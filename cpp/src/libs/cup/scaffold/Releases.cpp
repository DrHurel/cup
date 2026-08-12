module;
#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
module cup.scaffold;

import cup.platform;
import cup.ui;

namespace cup::scaffold {
namespace {

// Compiler majors ship a few times a year at most, so a week keeps `cup new`
// off the network on all but the occasional run.
constexpr auto kReleaseCacheTtl = std::chrono::hours(7 * 24);

std::filesystem::path release_cache_dir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "cup";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "") / ".cache" / "cup";
}

// Reads a "key":N field out of the tiny, fixed-shape JSON object this module
// writes itself (see write_release_cache) — not a general JSON parser.
std::optional<long long> extract_number(std::string_view text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const auto pos = text.find(needle);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t i = pos + needle.size();
    const bool negative = i < text.size() && text[i] == '-';
    if (negative) {
        ++i;
    }
    const std::size_t digits_start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
    }
    if (i == digits_start) {
        return std::nullopt;
    }
    long long value = 0;
    for (std::size_t k = digits_start; k < i; ++k) {
        value = value * 10 + (text[k] - '0');
    }
    return negative ? -value : value;
}

}

std::string release_cache_path() { return (release_cache_dir() / "compiler-releases.json").string(); }

std::optional<ReleaseCache> read_release_cache() {
    std::ifstream in(release_cache_path(), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto gcc = extract_number(text, "gcc");
    const auto clang = extract_number(text, "clang");
    const auto fetched_at = extract_number(text, "fetched_at");
    if (!gcc.has_value() || !clang.has_value() || !fetched_at.has_value()) {
        return std::nullopt;
    }
    return ReleaseCache{static_cast<int>(*gcc), static_cast<int>(*clang), *fetched_at};
}

std::expected<void, error::Error> write_release_cache(const ReleaseCache& cache) {
    const auto path = release_cache_path();
    std::error_code ec;
    std::filesystem::create_directories(release_cache_dir(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\"gcc\":" << cache.gcc << ",\"clang\":" << cache.clang
        << ",\"fetched_at\":" << cache.fetched_at_epoch_seconds << "}";
    if (!out) {
        return std::unexpected(error::Error("writing " + path));
    }
    return {};
}

int fetch_gcc_newest() {
    const auto body = platform::http_get(gcc_releases_url());
    if (!body.has_value()) {
        return 0;
    }
    return detail::parse_gcc_newest(*body);
}

int fetch_clang_newest() {
    const auto body = platform::http_get(clang_releases_url());
    if (!body.has_value()) {
        return 0;
    }
    return detail::parse_clang_newest(*body);
}

// Resolves the ceiling from, in order of preference: a fresh on-disk cache, a
// live fetch (cached on success), the last cached value, and finally the
// bundled fallback constants — so it always returns usable versions and only
// reaches the network about once a week.
std::pair<int, int> fetch_newest_compilers() {
    const auto now_epoch =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    const auto ttl_seconds = std::chrono::duration_cast<std::chrono::seconds>(kReleaseCacheTtl).count();
    if (const auto cached = read_release_cache();
        cached.has_value() && (now_epoch - cached->fetched_at_epoch_seconds) < ttl_seconds) {
        return {cached->gcc, cached->clang};
    }
    ui::running("checking latest gcc/clang releases");

    int gcc = 0;
    int clang = 0;
    std::thread gcc_thread([&gcc] { gcc = fetch_gcc_newest(); });
    std::thread clang_thread([&clang] { clang = fetch_clang_newest(); });
    gcc_thread.join();
    clang_thread.join();

    const auto cached = read_release_cache();
    if (gcc == 0) {
        gcc = detail::first_non_zero(cached.has_value() ? cached->gcc : 0, kGccNewestFallback);
    }
    if (clang == 0) {
        clang = detail::first_non_zero(cached.has_value() ? cached->clang : 0, kClangNewestFallback);
    }
    (void)write_release_cache({gcc, clang, now_epoch});
    return {gcc, clang};
}

}
