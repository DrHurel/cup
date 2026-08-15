#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "TempDir.hpp"

import cup.log;

namespace {

using cup::test::TempDir;

// Sets an environment variable for the test's lifetime, restoring (or
// unsetting) it afterward. Mirrors template_completion_test.cpp's ScopedEnv
// and releases_test.cpp's ScopedCacheHome — this port's established
// convention is to duplicate small per-file test helpers rather than share
// them.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::string_view value) : name_(name) {
        if (const char* prev = std::getenv(name); prev != nullptr) {
            previous_ = prev;
        }
        ::setenv(name_, std::string(value).c_str(), 1);
    }
    ~ScopedEnv() {
        if (previous_.has_value()) {
            ::setenv(name_, previous_->c_str(), 1);
        } else {
            ::unsetenv(name_);
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("cup.log writes leveled entries under XDG_CACHE_HOME/cup/cup.log", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv unset_level("CUP_LOG", "info");

    cup::log::init();
    cup::log::debug("should not appear at the default info level");
    cup::log::info("hello info");
    cup::log::warn("hello warn");
    cup::log::error("hello error");

    const auto log_path = cache.path() / "cup" / "cup.log";
    REQUIRE(std::filesystem::exists(log_path));
    const std::string content = read_file(log_path);
    CHECK(content.find("[info] hello info") != std::string::npos);
    CHECK(content.find("[warning] hello warn") != std::string::npos);
    CHECK(content.find("[error] hello error") != std::string::npos);
    CHECK(content.find("should not appear") == std::string::npos);
}

TEST_CASE("CUP_LOG raises the level filter", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv level("CUP_LOG", "error");

    cup::log::init();
    cup::log::info("should be filtered out");
    cup::log::error("should be kept");

    const std::string content = read_file(cache.path() / "cup" / "cup.log");
    CHECK(content.find("should be kept") != std::string::npos);
    CHECK(content.find("should be filtered out") == std::string::npos);
}

TEST_CASE("CUP_LOG=off disables the log file entirely", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv level("CUP_LOG", "off");

    cup::log::init();
    cup::log::error("must not be written anywhere");

    CHECK_FALSE(std::filesystem::exists(cache.path() / "cup" / "cup.log"));
}
