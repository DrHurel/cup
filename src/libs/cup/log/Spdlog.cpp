module;
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
module cup.log;

namespace cup::log {
namespace {

constexpr std::size_t kMaxFileBytes = 5 * 1024 * 1024;
constexpr std::size_t kMaxFiles = 3;

enum class Category : std::size_t { User = 0, Internal = 1, External = 2 };
constexpr std::size_t kCategoryCount = 3;
constexpr std::array<std::string_view, kCategoryCount> kCategoryNames{"user", "internal", "external"};
constexpr std::array<const char*, kCategoryCount> kCategoryEnvVars{"CUP_LOG_USER", "CUP_LOG_INTERNAL",
                                                                   "CUP_LOG_EXTERNAL"};

// log_dir mirrors scaffold::release_cache_dir()'s XDG_CACHE_HOME/HOME
// resolution (src/libs/cup/scaffold/Releases.cpp) — duplicated rather than
// shared, matching this port's established per-file convention for small
// helpers (see Thirdparty.cpp's kAptMarker comment).
std::filesystem::path log_dir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "cup";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "") / ".cache" / "cup";
}

spdlog::level::level_enum parse_level(std::string_view level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "off") return spdlog::level::off;
    return spdlog::level::info;
}

// level_for resolves one category's level: its own CUP_LOG_<CATEGORY> env
// var if set, else the shared CUP_LOG default, else "info" — each category
// gets independent control over its own criticality threshold, while a
// single CUP_LOG knob still covers the common case.
spdlog::level::level_enum level_for(const char* category_var) {
    if (const char* raw = std::getenv(category_var); raw != nullptr) {
        return parse_level(raw);
    }
    if (const char* shared = std::getenv("CUP_LOG"); shared != nullptr) {
        return parse_level(shared);
    }
    return spdlog::level::info;
}

// active_loggers holds cup's own log handles, one per Category — null until
// init() succeeds for that category (its level resolved to "off", or the
// log file couldn't be opened), in which case that category's debug/info/
// warn/error are silent no-ops. A plain static rather than spdlog's global
// registry: re-running init() (e.g. once per Catch2 test case in the same
// process) just replaces these pointers instead of hitting spdlog's
// duplicate-logger-name registry error.
std::array<std::shared_ptr<spdlog::logger>, kCategoryCount>& active_loggers() {
    static std::array<std::shared_ptr<spdlog::logger>, kCategoryCount> loggers;
    return loggers;
}

void log_at(Category category, spdlog::level::level_enum level, std::string_view msg) {
    if (const auto& logger = active_loggers()[std::to_underlying(category)]) {
        logger->log(level, msg);
    }
}

}  // namespace

std::expected<void, error::Error> init() {
    for (auto& logger : active_loggers()) {
        logger.reset();
    }

    std::array<spdlog::level::level_enum, kCategoryCount> levels{};
    bool any_enabled = false;
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
        levels[i] = level_for(kCategoryEnvVars[i]);
        any_enabled = any_enabled || levels[i] != spdlog::level::off;
    }
    if (!any_enabled) {
        return {};
    }

    const auto path = log_dir() / "cup.log";
    try {
        std::error_code ec;
        std::filesystem::create_directories(log_dir(), ec);
        // One sink shared by every enabled category's logger — a single
        // file, one line per call, tagged with its category via %n so the
        // categories stay distinguishable despite the shared destination.
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path.string(), kMaxFileBytes, kMaxFiles);
        for (std::size_t i = 0; i < kCategoryCount; ++i) {
            if (levels[i] == spdlog::level::off) {
                continue;
            }
            auto logger = std::make_shared<spdlog::logger>(std::string(kCategoryNames[i]), sink);
            logger->set_level(levels[i]);
            logger->set_pattern("[%Y-%m-%dT%H:%M:%S%z] [%l] [%n] %v");
            // Flush on every write, not just warn+: cup is a short-lived
            // CLI that logs a handful of lines per invocation, so the I/O
            // cost is negligible — and an info-only category's line must
            // still reach disk if the process is killed rather than exiting
            // cleanly (a clean exit would flush via the file handle's own
            // destructor either way).
            logger->flush_on(spdlog::level::trace);
            active_loggers()[i] = std::move(logger);
        }
        return {};
    } catch (const spdlog::spdlog_ex& e) {
        // Logging must never break the CLI: active_loggers() stays null, so
        // debug/info/warn/error below become no-ops — but the failure is
        // reported rather than swallowed, so the caller can tell the user
        // and point at CUP_LOG=off as the way to turn logging off outright.
        return std::unexpected(error::Error(std::format(
            "cup.log: could not open {} ({}) — set CUP_LOG=off to disable logging", path.string(),
            e.what())));
    }
}

namespace user {
void debug(std::string_view msg) { log_at(Category::User, spdlog::level::debug, msg); }
void info(std::string_view msg) { log_at(Category::User, spdlog::level::info, msg); }
void warn(std::string_view msg) { log_at(Category::User, spdlog::level::warn, msg); }
void error(std::string_view msg) { log_at(Category::User, spdlog::level::err, msg); }
}  // namespace user

namespace internal {
void debug(std::string_view msg) { log_at(Category::Internal, spdlog::level::debug, msg); }
void info(std::string_view msg) { log_at(Category::Internal, spdlog::level::info, msg); }
void warn(std::string_view msg) { log_at(Category::Internal, spdlog::level::warn, msg); }
void error(std::string_view msg) { log_at(Category::Internal, spdlog::level::err, msg); }
}  // namespace internal

namespace external {
void debug(std::string_view msg) { log_at(Category::External, spdlog::level::debug, msg); }
void info(std::string_view msg) { log_at(Category::External, spdlog::level::info, msg); }
void warn(std::string_view msg) { log_at(Category::External, spdlog::level::warn, msg); }
void error(std::string_view msg) { log_at(Category::External, spdlog::level::err, msg); }
}  // namespace external

}  // namespace cup::log
