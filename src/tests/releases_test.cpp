#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "TempDir.hpp"
#include "TestHttpServer.hpp"

import cup.scaffold;

namespace {

using cup::test::TempDir;
using cup::test::TestHttpServer;

// Restores a mutable global (an overridable hook) to its prior value when the
// test ends, mirroring Go's `prev := X; t.Cleanup(func() { X = prev })`.
template <typename T>
class ScopedOverride {
public:
    ScopedOverride(T& slot, T value) : slot_(slot), previous_(slot) { slot_ = std::move(value); }
    ~ScopedOverride() { slot_ = std::move(previous_); }
    ScopedOverride(const ScopedOverride&) = delete;
    ScopedOverride& operator=(const ScopedOverride&) = delete;

private:
    T& slot_;
    T previous_;
};

// Points XDG_CACHE_HOME at a scratch directory for the test's lifetime, so
// the release cache never touches the real one.
class ScopedCacheHome {
public:
    explicit ScopedCacheHome(const std::filesystem::path& dir) {
        if (const char* prev = std::getenv("XDG_CACHE_HOME"); prev != nullptr) {
            previous_ = prev;
        }
        ::setenv("XDG_CACHE_HOME", dir.string().c_str(), 1);
    }
    ~ScopedCacheHome() {
        if (previous_.has_value()) {
            ::setenv("XDG_CACHE_HOME", previous_->c_str(), 1);
        } else {
            ::unsetenv("XDG_CACHE_HOME");
        }
    }
    ScopedCacheHome(const ScopedCacheHome&) = delete;
    ScopedCacheHome& operator=(const ScopedCacheHome&) = delete;

private:
    std::optional<std::string> previous_;
};

}

TEST_CASE("parse_gcc_newest finds the largest major in a directory listing",
          "[releases][parse_gcc_newest]") {
    const std::string index = R"(<a href="gcc-4.8.5/">gcc-4.8.5/</a>
<a href="gcc-14.2.0/">gcc-14.2.0/</a>
<a href="gcc-15.1.0/">gcc-15.1.0/</a>
<a href="summit/">summit/</a>)";
    REQUIRE(cup::scaffold::detail::parse_gcc_newest(index) == 15);
    REQUIRE(cup::scaffold::detail::parse_gcc_newest("no versions here") == 0);
}

TEST_CASE("parse_clang_newest skips prereleases and finds the largest major",
          "[releases][parse_clang_newest]") {
    const std::string body = R"([
        {"tag_name":"llvmorg-21.0.0-rc1","prerelease":true},
        {"tag_name":"llvmorg-20.1.8","prerelease":false},
        {"tag_name":"llvmorg-19.1.7","prerelease":false}
    ])";
    REQUIRE(cup::scaffold::detail::parse_clang_newest(body) == 20);
    REQUIRE(cup::scaffold::detail::parse_clang_newest("not json") == 0);
}

TEST_CASE("first_non_zero", "[releases][first_non_zero]") {
    REQUIRE(cup::scaffold::detail::first_non_zero(7, 3) == 7);
    REQUIRE(cup::scaffold::detail::first_non_zero(0, 3) == 3);
}

TEST_CASE("newest_compilers_func is the override point for newest_compilers",
          "[releases][newest_compilers]") {
    ScopedOverride override_func(cup::scaffold::newest_compilers_func(),
                                 cup::scaffold::NewestCompilersFunc{
                                     +[]() -> std::pair<int, int> { return {42, 99}; }});
    const auto [gcc, clang] = cup::scaffold::newest_compilers();
    REQUIRE(gcc == 42);
    REQUIRE(clang == 99);
}

TEST_CASE("release cache round-trips through disk", "[releases][cache]") {
    const TempDir cache_root;
    const ScopedCacheHome cache_home(cache_root.path());

    REQUIRE_FALSE(cup::scaffold::read_release_cache().has_value());

    const cup::scaffold::ReleaseCache want{15, 20, 1'700'000'000};
    REQUIRE(cup::scaffold::write_release_cache(want).has_value());

    const std::string path = cup::scaffold::release_cache_path();
    REQUIRE(std::filesystem::path(path).filename() == "compiler-releases.json");

    const auto got = cup::scaffold::read_release_cache();
    REQUIRE(got.has_value());
    REQUIRE(got->gcc == 15);
    REQUIRE(got->clang == 20);
}

TEST_CASE("fetch_gcc_newest / fetch_clang_newest", "[releases][fetch]") {
    {
        const TestHttpServer gcc_server([](const std::string&) {
            return TestHttpServer::Response{200, R"(<a href="gcc-14.2.0/">gcc-14.2.0/</a>)"};
        });
        const TestHttpServer clang_server([](const std::string&) {
            return TestHttpServer::Response{
                200, R"([{"tag_name":"llvmorg-19.1.0","prerelease":false}])"};
        });
        const ScopedOverride gcc_url(cup::scaffold::gcc_releases_url(), gcc_server.url());
        const ScopedOverride clang_url(cup::scaffold::clang_releases_url(), clang_server.url());

        REQUIRE(cup::scaffold::fetch_gcc_newest() == 14);
        REQUIRE(cup::scaffold::fetch_clang_newest() == 19);
    }
    // On a transport error both return 0 so the caller can fall back.
    {
        const TestHttpServer gcc_server(
            [](const std::string&) { return TestHttpServer::Response{500, ""}; });
        const TestHttpServer clang_server(
            [](const std::string&) { return TestHttpServer::Response{500, ""}; });
        const ScopedOverride gcc_url(cup::scaffold::gcc_releases_url(), gcc_server.url());
        const ScopedOverride clang_url(cup::scaffold::clang_releases_url(), clang_server.url());

        REQUIRE(cup::scaffold::fetch_gcc_newest() == 0);
        REQUIRE(cup::scaffold::fetch_clang_newest() == 0);
    }
}

TEST_CASE("fetch_newest_compilers fetches from the network and populates the cache",
          "[releases][fetch]") {
    const TempDir cache_root;
    const ScopedCacheHome cache_home(cache_root.path());

    const TestHttpServer gcc_server([](const std::string&) {
        return TestHttpServer::Response{200, R"(<a href="gcc-15.1.0/">gcc-15.1.0/</a>)"};
    });
    const TestHttpServer clang_server([](const std::string&) {
        return TestHttpServer::Response{200, R"([{"tag_name":"llvmorg-20.1.8","prerelease":false}])"};
    });
    const ScopedOverride gcc_url(cup::scaffold::gcc_releases_url(), gcc_server.url());
    const ScopedOverride clang_url(cup::scaffold::clang_releases_url(), clang_server.url());

    const auto [gcc, clang] = cup::scaffold::fetch_newest_compilers();
    REQUIRE(gcc == 15);
    REQUIRE(clang == 20);

    const auto cached = cup::scaffold::read_release_cache();
    REQUIRE(cached.has_value());
    REQUIRE(cached->gcc == 15);
    REQUIRE(cached->clang == 20);
}

TEST_CASE("fetch_newest_compilers falls back to the bundled constants when offline",
          "[releases][fetch]") {
    const TempDir cache_root;
    const ScopedCacheHome cache_home(cache_root.path());

    const TestHttpServer gcc_server(
        [](const std::string&) { return TestHttpServer::Response{500, ""}; });
    const TestHttpServer clang_server(
        [](const std::string&) { return TestHttpServer::Response{500, ""}; });
    const ScopedOverride gcc_url(cup::scaffold::gcc_releases_url(), gcc_server.url());
    const ScopedOverride clang_url(cup::scaffold::clang_releases_url(), clang_server.url());

    const auto [gcc, clang] = cup::scaffold::fetch_newest_compilers();
    REQUIRE(gcc == cup::scaffold::kGccNewestFallback);
    REQUIRE(clang == cup::scaffold::kClangNewestFallback);
}

TEST_CASE("fetch_newest_compilers serves a fresh cache without touching the network",
          "[releases][fetch]") {
    const TempDir cache_root;
    const ScopedCacheHome cache_home(cache_root.path());
    REQUIRE(cup::scaffold::write_release_cache({13, 18, [] {
                                                    return std::chrono::duration_cast<
                                                               std::chrono::seconds>(
                                                               std::chrono::system_clock::now()
                                                                   .time_since_epoch())
                                                        .count();
                                                }()})
                .has_value());

    // Servers would return other numbers; a fresh cache must short-circuit before them.
    const TestHttpServer gcc_server([](const std::string&) {
        return TestHttpServer::Response{200, R"(<a href="gcc-99.1.0/">gcc-99.1.0/</a>)"};
    });
    const TestHttpServer clang_server([](const std::string&) {
        return TestHttpServer::Response{200, R"([{"tag_name":"llvmorg-99.1.0","prerelease":false}])"};
    });
    const ScopedOverride gcc_url(cup::scaffold::gcc_releases_url(), gcc_server.url());
    const ScopedOverride clang_url(cup::scaffold::clang_releases_url(), clang_server.url());

    const auto [gcc, clang] = cup::scaffold::fetch_newest_compilers();
    REQUIRE(gcc == 13);
    REQUIRE(clang == 18);
}
