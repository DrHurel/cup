// Port of internal/scaffold/compiler_releases_test.go, plus the two release parsers
// tested in compiler_test.go — those follow the C++ partition split (:releases)
// rather than the Go file split.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.
//
// Nothing here touches the network or the user's cache directory. Go points its
// fetchers at an httptest server; here the substituted seam is one level up —
// cup.platform's http_get — because standing up an HTTP server to test an HTTP
// client is a worse trade in C++ than in Go. The cache is redirected with
// XDG_CACHE_HOME, exactly as the Go suite does.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>  // setenv / unsetenv, for XDG_CACHE_HOME

// write_release_cache returns std::expected<void, Error>, whose void specialisation
// a module cannot re-export from its global module fragment — see the note in
// ui_test.cpp.
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "TempDir.hpp"

import cup.scaffold;
import cup.platform;

namespace {

using cup::scaffold::Compilers;
using cup::scaffold::detail::ReleaseCache;
using cup::test::TempDir;

// The stubbed fetcher's state. It is file-scope because HttpGet is a plain function
// pointer — see the note on it in cup.platform's Http.cppm — so the replacement
// cannot capture. That is no worse than the Go suite, whose stubs are package-level
// variables for the same practical reason.
struct HttpStub {
    std::string gcc_body;
    std::string clang_body;
    bool fail = false;
    std::vector<std::string> urls;
};

HttpStub& stub() {
    static HttpStub state;
    return state;
}

[[nodiscard]] std::expected<std::string, cup::error::Error> stub_get(std::string_view url) {
    {
        // fetch_newest_compilers asks for both lists at once, so two threads reach
        // this — the same reason the real fetcher initialises libcurl under a
        // std::call_once.
        static std::mutex recording;
        const std::lock_guard<std::mutex> held(recording);
        stub().urls.emplace_back(url);
    }
    if (stub().fail) {
        return std::unexpected(cup::error::Error("GET failed: 500 Internal Server Error"));
    }
    // The two fetchers are told apart by their endpoint, which is what lets one
    // stub serve both halves of a concurrent fetch.
    return url.contains("ftp.gnu.org") ? stub().gcc_body : stub().clang_body;
}

// ScopedReleaseBodies points the fetchers at fixed replies for one test.
// (Go: withReleaseServers.)
class ScopedReleaseBodies {
public:
    ScopedReleaseBodies(std::string gcc_body, std::string clang_body, bool fail = false) {
        stub() = HttpStub{.gcc_body = std::move(gcc_body),
                          .clang_body = std::move(clang_body),
                          .fail = fail};
    }
    ScopedReleaseBodies(const ScopedReleaseBodies&) = delete;
    ScopedReleaseBodies& operator=(const ScopedReleaseBodies&) = delete;
    ~ScopedReleaseBodies() { stub() = HttpStub{}; }

private:
    cup::platform::ScopedHttpGet installed_{stub_get};
};

// ScopedCacheDir redirects the release cache into a temp directory, so no test
// reads or writes the developer's real one. (Go: t.Setenv("XDG_CACHE_HOME", …),
// which os.UserCacheDir honours on Linux — and so does release_cache_path.)
class ScopedCacheDir {
public:
    explicit ScopedCacheDir(const std::filesystem::path& dir) {
        if (const char* previous = std::getenv("XDG_CACHE_HOME"); previous != nullptr) {
            had_previous_ = true;
            previous_ = previous;
        }
        ::setenv("XDG_CACHE_HOME", dir.c_str(), 1);
    }
    ScopedCacheDir(const ScopedCacheDir&) = delete;
    ScopedCacheDir& operator=(const ScopedCacheDir&) = delete;
    ~ScopedCacheDir() {
        if (had_previous_) {
            ::setenv("XDG_CACHE_HOME", previous_.c_str(), 1);
        } else {
            ::unsetenv("XDG_CACHE_HOME");
        }
    }

private:
    bool had_previous_ = false;
    std::string previous_;
};

}  // namespace

// Go: TestFirstNonZero
TEST_CASE("first_non_zero prefers the first usable value", "[scaffold][releases]") {
    REQUIRE(cup::scaffold::detail::first_non_zero(7, 3) == 7);
    REQUIRE(cup::scaffold::detail::first_non_zero(0, 3) == 3);
}

// Go: TestNewestCompilersFunc
TEST_CASE("newest_compilers reads the installed source", "[scaffold][releases]") {
    const cup::scaffold::ScopedNewestCompilers scoped([] {
        return Compilers{.gcc = 42, .clang = 99};
    });
    REQUIRE(cup::scaffold::newest_compilers() == Compilers{.gcc = 42, .clang = 99});
}

// Go: TestParseGCCNewest
TEST_CASE("parse_gcc_newest takes the largest major in the index", "[scaffold][releases]") {
    constexpr std::string_view kIndex = R"(<a href="gcc-4.8.5/">gcc-4.8.5/</a>
<a href="gcc-14.2.0/">gcc-14.2.0/</a>
<a href="gcc-15.1.0/">gcc-15.1.0/</a>
<a href="summit/">summit/</a>)";
    REQUIRE(cup::scaffold::detail::parse_gcc_newest(kIndex) == 15);
    REQUIRE(cup::scaffold::detail::parse_gcc_newest("no versions here") == 0);
}

// Go: TestParseClangNewest
TEST_CASE("parse_clang_newest skips prereleases", "[scaffold][releases]") {
    constexpr std::string_view kBody = R"([
        {"tag_name":"llvmorg-21.0.0-rc1","prerelease":true},
        {"tag_name":"llvmorg-20.1.8","prerelease":false},
        {"tag_name":"llvmorg-19.1.7","prerelease":false}
    ])";
    REQUIRE(cup::scaffold::detail::parse_clang_newest(kBody) == 20);
    // A reply cup cannot parse yields no version rather than an exception.
    REQUIRE(cup::scaffold::detail::parse_clang_newest("not json") == 0);
    REQUIRE(cup::scaffold::detail::parse_clang_newest(R"({"not":"an array"})") == 0);
}

// Go: TestFetchGCCAndClangNewest
TEST_CASE("the fetchers read their endpoint and degrade to 0", "[scaffold][releases]") {
    {
        const ScopedReleaseBodies bodies(R"(<a href="gcc-14.2.0/">gcc-14.2.0/</a>)",
                                         R"([{"tag_name":"llvmorg-19.1.0","prerelease":false}])");
        REQUIRE(cup::scaffold::detail::fetch_gcc_newest() == 14);
        REQUIRE(cup::scaffold::detail::fetch_clang_newest() == 19);

        // Each fetcher asked for its own list, and for nothing else.
        REQUIRE(stub().urls.size() == 2);
        REQUIRE(stub().urls[0] == cup::scaffold::detail::kGccReleasesUrl);
        REQUIRE(stub().urls[1] == cup::scaffold::detail::kClangReleasesUrl);
    }

    // On a transport error both report 0, so the caller can fall back.
    const ScopedReleaseBodies failing("", "", /*fail=*/true);
    REQUIRE(cup::scaffold::detail::fetch_gcc_newest() == 0);
    REQUIRE(cup::scaffold::detail::fetch_clang_newest() == 0);
}

// Go: TestReleaseCacheRoundTrip
TEST_CASE("the release cache round-trips through the cache dir", "[scaffold][releases][cache]") {
    const TempDir cache;
    const ScopedCacheDir scoped(cache);

    REQUIRE_FALSE(cup::scaffold::detail::read_release_cache().has_value());

    const std::int64_t when = cup::scaffold::detail::now_unix();
    REQUIRE(cup::scaffold::detail::write_release_cache(
                ReleaseCache{.gcc = 15, .clang = 20, .fetched_at = when})
                .has_value());

    const auto path = cup::scaffold::detail::release_cache_path();
    REQUIRE(path.has_value());
    REQUIRE(std::filesystem::path(*path).filename() == "compiler-releases.json");

    const auto got = cup::scaffold::detail::read_release_cache();
    REQUIRE(got.has_value());
    REQUIRE(got->gcc == 15);
    REQUIRE(got->clang == 20);
    // The timestamp survives a trip through RFC 3339, which is the format the Go
    // cup reads and writes in the same file.
    REQUIRE(got->fetched_at == when);
}

// No Go counterpart: Go's encoding/json handles the timestamp, while this
// implementation parses RFC 3339 itself — so the shapes Go emits are pinned here.
TEST_CASE("the cache reads the timestamps Go writes", "[scaffold][releases][cache]") {
    const TempDir cache;
    const ScopedCacheDir scoped(cache);
    const auto path = cup::scaffold::detail::release_cache_path();
    REQUIRE(path.has_value());

    const auto stored = [&path](std::string_view document) {
        std::filesystem::create_directories(std::filesystem::path(*path).parent_path());
        std::ofstream out(*path, std::ios::binary | std::ios::trunc);
        out << document;
        out.close();
        return cup::scaffold::detail::read_release_cache();
    };

    SECTION("a UTC instant, as this implementation writes it") {
        const auto got = stored(R"({"gcc":15,"clang":20,"fetched_at":"2026-07-26T10:11:12Z"})");
        REQUIRE(got.has_value());
        REQUIRE(got->fetched_at == 1785060672);
    }

    SECTION("fractional seconds and an offset, as Go's time.Time marshals them") {
        // The same instant, written the way a Go cup on a +02:00 machine records it.
        const auto got = stored(
            R"({"gcc":15,"clang":20,"fetched_at":"2026-07-26T12:11:12.123456789+02:00"})");
        REQUIRE(got.has_value());
        REQUIRE(got->fetched_at == 1785060672);
    }

    SECTION("a timestamp that cannot be read is a cache miss, not a stale hit") {
        REQUIRE_FALSE(stored(R"({"gcc":15,"clang":20,"fetched_at":"yesterday"})").has_value());
        REQUIRE_FALSE(stored(R"({"gcc":15,"clang":20})").has_value());
        REQUIRE_FALSE(stored("not json").has_value());
    }
}

// Go: TestFetchNewestCompilersFromNetwork
TEST_CASE("fetch_newest_compilers fetches and then caches", "[scaffold][releases]") {
    const TempDir cache;
    const ScopedCacheDir scoped(cache);  // no cache -> forces the fetch path
    const ScopedReleaseBodies bodies(R"(<a href="gcc-15.1.0/">gcc-15.1.0/</a>)",
                                     R"([{"tag_name":"llvmorg-20.1.8","prerelease":false}])");

    REQUIRE(cup::scaffold::detail::fetch_newest_compilers() ==
            Compilers{.gcc = 15, .clang = 20});

    const auto cached = cup::scaffold::detail::read_release_cache();
    REQUIRE(cached.has_value());
    REQUIRE(cached->gcc == 15);
    REQUIRE(cached->clang == 20);
}

// Go: TestFetchNewestCompilersFallback
TEST_CASE("an unreachable network falls back to the constants", "[scaffold][releases]") {
    const TempDir cache;
    const ScopedCacheDir scoped(cache);
    // Both endpoints fail and nothing is cached, so the bundled fallbacks win —
    // which is what makes `cup new` work on a plane.
    const ScopedReleaseBodies failing("", "", /*fail=*/true);

    REQUIRE(cup::scaffold::detail::fetch_newest_compilers() ==
            Compilers{.gcc = cup::scaffold::detail::kGccNewestFallback,
                      .clang = cup::scaffold::detail::kClangNewestFallback});
}

// Go: TestFetchNewestCompilersServesFreshCache
TEST_CASE("a fresh cache short-circuits the fetch", "[scaffold][releases]") {
    const TempDir cache;
    const ScopedCacheDir scoped(cache);
    REQUIRE(cup::scaffold::detail::write_release_cache(
                ReleaseCache{.gcc = 13,
                             .clang = 18,
                             .fetched_at = cup::scaffold::detail::now_unix()})
                .has_value());

    // The stub would answer with other numbers; a fresh cache must not reach it.
    const ScopedReleaseBodies bodies(R"(<a href="gcc-99.1.0/">gcc-99.1.0/</a>)",
                                     R"([{"tag_name":"llvmorg-99.1.0","prerelease":false}])");

    REQUIRE(cup::scaffold::detail::fetch_newest_compilers() ==
            Compilers{.gcc = 13, .clang = 18});
    REQUIRE(stub().urls.empty());
}

// No Go counterpart: Go's releaseCacheTTL is exercised only through time.Since on a
// live clock, while the seconds here are explicit — so the expiry edge is testable.
TEST_CASE("a cache older than the TTL is refetched", "[scaffold][releases]") {
    const TempDir cache;
    const ScopedCacheDir scoped(cache);
    const std::int64_t stale =
        cup::scaffold::detail::now_unix() - cup::scaffold::detail::kReleaseCacheTtlSeconds - 1;
    REQUIRE(cup::scaffold::detail::write_release_cache(
                ReleaseCache{.gcc = 13, .clang = 18, .fetched_at = stale})
                .has_value());

    const ScopedReleaseBodies bodies(R"(<a href="gcc-16.1.0/">gcc-16.1.0/</a>)",
                                     R"([{"tag_name":"llvmorg-21.1.0","prerelease":false}])");

    REQUIRE(cup::scaffold::detail::fetch_newest_compilers() ==
            Compilers{.gcc = 16, .clang = 21});
    REQUIRE(stub().urls.size() == 2);
}

// No Go counterpart: os.UserCacheDir raises an error where this returns nullopt, and
// the callers have to treat it as "no cache" rather than as a failure.
TEST_CASE("no cache directory means no cache, not an error", "[scaffold][releases][cache]") {
    const ScopedCacheDir scoped("relative/not/absolute");
    REQUIRE_FALSE(cup::scaffold::detail::release_cache_path().has_value());
    REQUIRE_FALSE(cup::scaffold::detail::read_release_cache().has_value());
    REQUIRE_FALSE(cup::scaffold::detail::write_release_cache(ReleaseCache{}).has_value());
}
