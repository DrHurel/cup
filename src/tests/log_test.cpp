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

TEST_CASE("cup.log writes leveled, categorized entries under XDG_CACHE_HOME/cup/cup.log", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv unset_level("CUP_LOG", "info");

    REQUIRE(cup::log::init().has_value());
    cup::log::user::debug("should not appear at the default info level");
    cup::log::user::info("hello user");
    cup::log::internal::warn("hello internal");
    cup::log::external::error("hello external");

    const auto log_path = cache.path() / "cup" / "cup.log";
    REQUIRE(std::filesystem::exists(log_path));
    const std::string content = read_file(log_path);
    CHECK(content.find("[info] [user] hello user") != std::string::npos);
    CHECK(content.find("[warning] [internal] hello internal") != std::string::npos);
    CHECK(content.find("[error] [external] hello external") != std::string::npos);
    CHECK(content.find("should not appear") == std::string::npos);
}

TEST_CASE("CUP_LOG_USER raises just the user category's level filter", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv user_level("CUP_LOG_USER", "error");

    REQUIRE(cup::log::init().has_value());
    cup::log::user::info("should be filtered out");
    cup::log::user::error("should be kept");
    cup::log::internal::info("unaffected by CUP_LOG_USER, still at the default info level");

    const std::string content = read_file(cache.path() / "cup" / "cup.log");
    CHECK(content.find("should be kept") != std::string::npos);
    CHECK(content.find("should be filtered out") == std::string::npos);
    CHECK(content.find("unaffected by CUP_LOG_USER") != std::string::npos);
}

TEST_CASE("CUP_LOG sets the shared default for every category", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv level("CUP_LOG", "error");

    REQUIRE(cup::log::init().has_value());
    cup::log::user::info("filtered by the shared default");
    cup::log::internal::info("also filtered by the shared default");
    cup::log::external::error("kept");

    const std::string content = read_file(cache.path() / "cup" / "cup.log");
    CHECK(content.find("kept") != std::string::npos);
    CHECK(content.find("filtered by the shared default") == std::string::npos);
    CHECK(content.find("also filtered by the shared default") == std::string::npos);
}

TEST_CASE("CUP_LOG=off disables the log file entirely", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv level("CUP_LOG", "off");

    REQUIRE(cup::log::init().has_value());
    cup::log::user::error("must not be written anywhere");
    cup::log::internal::error("must not be written anywhere either");

    CHECK_FALSE(std::filesystem::exists(cache.path() / "cup" / "cup.log"));
}

TEST_CASE("a per-category override re-enables logging even under a shared CUP_LOG=off", "[log]") {
    const TempDir cache;
    const ScopedEnv xdg("XDG_CACHE_HOME", cache.path().string());
    const ScopedEnv level("CUP_LOG", "off");
    const ScopedEnv external_level("CUP_LOG_EXTERNAL", "info");

    REQUIRE(cup::log::init().has_value());
    cup::log::user::error("still off, must not appear");
    cup::log::external::info("only external is enabled");

    const auto log_path = cache.path() / "cup" / "cup.log";
    REQUIRE(std::filesystem::exists(log_path));
    const std::string content = read_file(log_path);
    CHECK(content.find("only external is enabled") != std::string::npos);
    CHECK(content.find("still off, must not appear") == std::string::npos);
}
