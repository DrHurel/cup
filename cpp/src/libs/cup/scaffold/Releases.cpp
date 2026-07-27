// Implementation unit for cup.scaffold:releases — discovering the newest released
// GCC and Clang, so the floor picker has a ceiling. Port of
// internal/scaffold/compiler_releases.go.
//
// This is the second platform seam of the port (the first is the terminal, the
// third is running a subprocess): everything network-shaped goes through
// cup.platform's http_get, and every failure here is absorbed rather than reported.
// A `cup new` run must work on a plane.
module;
// nlohmann/json is a large header, and this is exactly where it belongs: a module
// implementation unit's global module fragment never reaches a BMI, so no consumer
// of cup.scaffold pays for the parser and GCC 14 never has to merge it into one.
// That is the rule Phase 2 paid for with an ICE on toml++ — see cup.project's
// io.cppm — applied up front.
//
// The library is registered through cup (`FetchContent` in third_party/CMakeLists.txt)
// and used in its header form rather than as `import nlohmann.json;`: the module
// build wants `import std;`, which needs GCC 15 and CMake 3.30, and cup's own floor
// is deliberately GCC 14 without the std module. It is a mechanical switch once the
// floor moves.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <ios>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
module cup.scaffold;

import cup.platform;
import cup.ui;

namespace cup::scaffold {
namespace detail {
namespace {

// major_in returns the largest capture-group-1 integer across every match of
// pattern in text. Both release lists are "find the biggest major named anywhere in
// this reply", which is what makes one helper cover them.
[[nodiscard]] int largest_major(std::string_view text, const std::regex& pattern) {
    int newest = 0;
    const std::cregex_iterator end;
    for (std::cregex_iterator match(text.data(), text.data() + text.size(), pattern); match != end;
         ++match) {
        const std::string digits = (*match)[1].str();
        int value = 0;
        const auto [ptr, ec] =
            std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (ec == std::errc{} && value > newest) {
            newest = value;
        }
    }
    return newest;
}

// append_padded writes value in at least width digits, zero-padded — what a {:02}
// spec would do, if one could be used here. See format_rfc3339 below.
void append_padded(std::string& out, long long value, std::size_t width) {
    const std::string digits = std::to_string(value);
    if (digits.size() < width) {
        out.append(width - digits.size(), '0');
    }
    out += digits;
}

// format_rfc3339 renders Unix seconds the way Go's encoding/json renders a
// time.Time, so a cache written here stays readable by the Go cup.
//
// The padding is hand-rolled because std::format cannot be used for it. Both
// obvious spellings —
//
//     std::format("{:%FT%TZ}", when)                     // chrono's own formatter
//     std::format("{:04}-{:02}-{:02}T...", year, ...)    // plain integers
//
// compile without complaint and then throw at run time, from inside the format
// call:
//
//     format error: invalid width or precision in format-spec
//
// Five cases in scaffold_releases_test.cpp failed on it, and it is the sharpest
// kind of module bug precisely because it type-checks: the consteval check on the
// format string passes, and the parse that runs is a different one. The same code
// in a plain translation unit — and in a reduced module built outside cup — is
// fine, so the trigger is somewhere in this module graph rather than in the two
// lines themselves.
//
// The rule that falls out, and the reason nothing else in cup trips it: a format
// *spec* in a cup.scaffold implementation unit cannot be trusted. Plain "{}"
// substitution, which is all the rest of the port uses, works everywhere.
[[nodiscard]] std::string format_rfc3339(std::int64_t unix_seconds) {
    const std::chrono::sys_seconds when{std::chrono::seconds{unix_seconds}};
    const auto midnight = std::chrono::floor<std::chrono::days>(when);
    const std::chrono::year_month_day date{midnight};
    const std::chrono::hh_mm_ss time{when - midnight};
    std::string out;
    append_padded(out, static_cast<int>(date.year()), 4);
    out += '-';
    append_padded(out, static_cast<unsigned>(date.month()), 2);
    out += '-';
    append_padded(out, static_cast<unsigned>(date.day()), 2);
    out += 'T';
    append_padded(out, time.hours().count(), 2);
    out += ':';
    append_padded(out, time.minutes().count(), 2);
    out += ':';
    append_padded(out, time.seconds().count(), 2);
    out += 'Z';
    return out;
}

// digits reads a fixed-width decimal field out of a timestamp.
[[nodiscard]] std::optional<int> digits(std::string_view text, std::size_t at, std::size_t width) {
    if (at + width > text.size()) {
        return std::nullopt;
    }
    int value = 0;
    const char* const first = text.data() + at;
    const auto [ptr, ec] = std::from_chars(first, first + width, value);
    if (ec != std::errc{} || ptr != first + width) {
        return std::nullopt;
    }
    return value;
}

// parse_rfc3339 reads the timestamps Go writes: a date, a time, an optional
// fractional part, and either Z or a ±HH:MM offset. Anything else is nullopt, which
// the caller treats as "no usable cache" — the same outcome Go gets from a failed
// json.Unmarshal.
[[nodiscard]] std::optional<std::int64_t> parse_rfc3339(std::string_view text) {
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') {
        return std::nullopt;
    }
    const auto year = digits(text, 0, 4);
    const auto month = digits(text, 5, 2);
    const auto day = digits(text, 8, 2);
    const auto hour = digits(text, 11, 2);
    const auto minute = digits(text, 14, 2);
    const auto second = digits(text, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second) {
        return std::nullopt;
    }

    const std::chrono::year_month_day date{std::chrono::year{*year},
                                           std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) {
        return std::nullopt;
    }
    const std::chrono::sys_seconds when = std::chrono::sys_days{date} +
                                          std::chrono::hours{*hour} +
                                          std::chrono::minutes{*minute} +
                                          std::chrono::seconds{*second};
    std::int64_t unix_seconds = when.time_since_epoch().count();

    // Skip the fractional seconds Go emits when the clock has them.
    std::size_t at = 19;
    if (at < text.size() && text[at] == '.') {
        ++at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            ++at;
        }
    }
    if (at >= text.size()) {
        return std::nullopt;  // no zone: not RFC 3339
    }
    if (text[at] == 'Z' || text[at] == 'z') {
        return unix_seconds;
    }
    if (text[at] != '+' && text[at] != '-') {
        return std::nullopt;
    }
    const bool ahead = text[at] == '+';
    const auto offset_hours = digits(text, at + 1, 2);
    const auto offset_minutes = digits(text, at + 4, 2);
    if (!offset_hours || !offset_minutes) {
        return std::nullopt;
    }
    const std::int64_t offset = (*offset_hours * 3600LL) + (*offset_minutes * 60LL);
    // An offset ahead of UTC means the wall clock reads later than UTC, so it comes
    // back off to reach the instant.
    return ahead ? unix_seconds - offset : unix_seconds + offset;
}

}  // namespace

int first_non_zero(int a, int b) { return a != 0 ? a : b; }

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int parse_gcc_newest(std::string_view body) {
    static const std::regex kGccDir(R"(gcc-(\d+)\.\d+)");
    return largest_major(body, kGccDir);
}

int parse_clang_newest(std::string_view body) {
    // parse(..., nullptr, false) is the non-throwing overload: a malformed reply
    // comes back discarded rather than as an exception, which is what lets every
    // caller here treat "bad JSON" as "no data" — the same shape as Go discarding
    // its json.Unmarshal error.
    const auto document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_array()) {
        return 0;
    }

    static const std::regex kClangTag(R"(llvmorg-(\d+)\.)");
    int newest = 0;
    for (const auto& release : document) {
        if (!release.is_object() || release.value("prerelease", false)) {
            continue;  // a release candidate is not a released major
        }
        const auto tag = release.value("tag_name", std::string{});
        newest = std::max(newest, largest_major(tag, kClangTag));
    }
    return newest;
}

int fetch_gcc_newest() {
    const auto body = platform::http_get(kGccReleasesUrl);
    return body.has_value() ? parse_gcc_newest(*body) : 0;
}

int fetch_clang_newest() {
    const auto body = platform::http_get(kClangReleasesUrl);
    return body.has_value() ? parse_clang_newest(*body) : 0;
}

std::optional<std::string> release_cache_path() {
    // os.UserCacheDir's rules on Linux: XDG_CACHE_HOME when it is set and absolute,
    // otherwise $HOME/.cache. A relative XDG_CACHE_HOME is an error there, and "no
    // cache" here — cup then simply refetches, which is the same degradation as a
    // cold cache.
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        const std::filesystem::path dir(xdg);
        if (!dir.is_absolute()) {
            return std::nullopt;
        }
        return (dir / "cup" / "compiler-releases.json").string();
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return (std::filesystem::path(home) / ".cache" / "cup" / "compiler-releases.json").string();
    }
    return std::nullopt;
}

std::optional<ReleaseCache> read_release_cache() {
    const auto path = release_cache_path();
    if (!path.has_value()) {
        return std::nullopt;
    }
    std::ifstream in(*path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::nullopt;
    }
    // An unreadable timestamp fails the whole read rather than defaulting to the
    // epoch: "cache of unknown age" is not something to reason about, and a miss
    // costs one fetch.
    const auto fetched_at = parse_rfc3339(document.value("fetched_at", std::string{}));
    if (!fetched_at.has_value()) {
        return std::nullopt;
    }
    return ReleaseCache{.gcc = document.value("gcc", 0),
                        .clang = document.value("clang", 0),
                        .fetched_at = *fetched_at};
}

std::expected<void, utils::error::Error> write_release_cache(const ReleaseCache& cache) {
    const auto path = release_cache_path();
    if (!path.has_value()) {
        return std::unexpected(utils::error::Error("no user cache directory"));
    }

    const std::filesystem::path file(*path);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        const std::string where = file.parent_path().string();
        const std::string why = ec.message();
        return std::unexpected(utils::error::Error(std::format("creating {}: {}", where, why)));
    }

    // ordered_json, not json: the plain type sorts its keys, and this file is shared
    // with the Go cup for as long as both binaries exist, so it is written in the
    // field order Go's encoding/json emits for its releaseCache struct.
    const nlohmann::ordered_json document{{"gcc", cache.gcc},
                                          {"clang", cache.clang},
                                          {"fetched_at", format_rfc3339(cache.fetched_at)}};

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << document.dump();
    if (!out) {
        const std::string where = file.string();
        return std::unexpected(utils::error::Error(std::format("writing {}", where)));
    }
    return {};
}

Compilers fetch_newest_compilers() {
    if (const auto cached = read_release_cache();
        cached.has_value() && now_unix() - cached->fetched_at < kReleaseCacheTtlSeconds) {
        return {.gcc = cached->gcc, .clang = cached->clang};
    }
    ui::running("checking latest gcc/clang releases");

    // The two lists are independent and each is a network round trip, so they go out
    // together. (Go: two goroutines and a WaitGroup.)
    auto gcc = std::async(std::launch::async, fetch_gcc_newest);
    auto clang = std::async(std::launch::async, fetch_clang_newest);
    Compilers newest{.gcc = gcc.get(), .clang = clang.get()};

    // A failed fetch falls back to whatever was cached — even if it is stale, it is
    // closer to the truth than a constant compiled in months ago.
    const ReleaseCache cached = read_release_cache().value_or(ReleaseCache{});
    if (newest.gcc == 0) {
        newest.gcc = first_non_zero(cached.gcc, kGccNewestFallback);
    }
    if (newest.clang == 0) {
        newest.clang = first_non_zero(cached.clang, kClangNewestFallback);
    }

    // Best effort: a cache that cannot be written costs a fetch next time and
    // nothing else.
    std::ignore = write_release_cache(
        ReleaseCache{.gcc = newest.gcc, .clang = newest.clang, .fetched_at = now_unix()});
    return newest;
}

}  // namespace detail

Compilers newest_compilers() { return detail::current_newest_compilers()(); }

}  // namespace cup::scaffold
