module;
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>
module cup.log;

namespace cup::log {
namespace {

constexpr std::size_t kMaxFileBytes = 5 * 1024 * 1024;
constexpr std::size_t kMaxFiles = 3;

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

spdlog::level::level_enum level_from_env() {
    const char* raw = std::getenv("CUP_LOG");
    if (raw == nullptr) {
        return spdlog::level::info;
    }
    const std::string_view level(raw);
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "off") return spdlog::level::off;
    return spdlog::level::info;
}

// active_logger holds cup's own log handle — null until init() succeeds, or
// whenever CUP_LOG=off or the log file couldn't be opened, in which case
// debug/info/warn/error are silent no-ops. A plain static rather than
// spdlog's global registry: re-running init() (e.g. once per Catch2 test
// case in the same process) just replaces this pointer instead of hitting
// spdlog's duplicate-logger-name registry error.
std::shared_ptr<spdlog::logger>& active_logger() {
    static std::shared_ptr<spdlog::logger> logger;
    return logger;
}

}  // namespace

void init() {
    active_logger().reset();
    const auto level = level_from_env();
    if (level == spdlog::level::off) {
        return;
    }
    try {
        const auto dir = log_dir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (dir / "cup.log").string(), kMaxFileBytes, kMaxFiles);
        auto logger = std::make_shared<spdlog::logger>("cup", sink);
        logger->set_level(level);
        logger->set_pattern("[%Y-%m-%dT%H:%M:%S%z] [%l] %v");
        logger->flush_on(spdlog::level::warn);
        active_logger() = std::move(logger);
    } catch (const spdlog::spdlog_ex&) {
        // Logging must never break the CLI: active_logger() stays null, so
        // debug/info/warn/error below become no-ops instead of propagating.
    }
}

void debug(std::string_view msg) {
    if (const auto& logger = active_logger()) {
        logger->debug(msg);
    }
}

void info(std::string_view msg) {
    if (const auto& logger = active_logger()) {
        logger->info(msg);
    }
}

void warn(std::string_view msg) {
    if (const auto& logger = active_logger()) {
        logger->warn(msg);
    }
}

void error(std::string_view msg) {
    if (const auto& logger = active_logger()) {
        logger->error(msg);
    }
}

}  // namespace cup::log
