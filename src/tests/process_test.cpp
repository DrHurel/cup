#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TempDir.hpp"

import cup.platform;

// Portable coverage of cup.platform's process and env layers: every case here
// runs a real child (child_helper.cpp) rather than sh/true/false, so the same
// suite exercises fork/execvp on POSIX and CreateProcessW — including its
// command-line quoting — on Windows. platform_test.cpp keeps the POSIX-only
// cases (a pty, injected fork/pipe failures, signals).

namespace {

using cup::test::TempDir;

constexpr std::string_view kHelper = CUP_CHILD_HELPER;

std::filesystem::path here() { return std::filesystem::current_path(); }

// Sets (or, with nullopt, unsets) an environment variable for the test's
// lifetime, restoring the previous value afterward. Duplicated per file, as
// log_test.cpp's ScopedEnv explains.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::optional<std::string_view> value) : name_(name) {
        if (const char* prev = std::getenv(name); prev != nullptr) {
            previous_ = prev;
        }
        if (value.has_value()) {
            ::setenv(name_, std::string(*value).c_str(), 1);
        } else {
            ::unsetenv(name_);
        }
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

}  // namespace

TEST_CASE("capture_command hands every argument to the child unchanged", "[process]") {
    // The cases Windows' flat command line has to quote and escape to survive
    // CommandLineToArgvW: spaces, embedded quotes, backslashes before a quote
    // and at the end, and the empty string.
    const std::vector<std::string> args{
        "args",          "plain",        "with space", R"(say "hi")", R"(C:\dir\)",
        R"(back\\slash)", R"(\"quoted\")", "",           "tab\there",
    };
    auto out = cup::platform::capture_command(here(), kHelper, args);
    REQUIRE(out.has_value());
    std::string expected;
    for (std::size_t i = 1; i < args.size(); ++i) {
        expected += "<" + args[i] + ">\n";
    }
    // A Windows child's text-mode stdout writes "\r\n".
    std::string got = *out;
    std::erase(got, '\r');
    REQUIRE(got == expected);
}

TEST_CASE("run_command succeeds for a zero exit and reports a non-zero one", "[process]") {
    REQUIRE(cup::platform::run_command(here(), kHelper, std::vector<std::string>{"exit", "0"})
                .has_value());

    const auto failed =
        cup::platform::run_command(here(), kHelper, std::vector<std::string>{"exit", "3"});
    REQUIRE_FALSE(failed.has_value());
    REQUIRE(failed.error().message().contains("exit status 3"));
}

TEST_CASE("capture_command reports a non-zero exit as an error", "[process]") {
    const auto failed =
        cup::platform::capture_command(here(), kHelper, std::vector<std::string>{"exit", "7"});
    REQUIRE_FALSE(failed.has_value());
    REQUIRE(failed.error().message().contains("exit status 7"));
}

TEST_CASE("run_command and capture_command run the child in dir", "[process]") {
    const TempDir dir;
    auto out = cup::platform::capture_command(dir.path(), kHelper, std::vector<std::string>{"cwd"});
    REQUIRE(out.has_value());
    std::string printed = *out;
    while (!printed.empty() && (printed.back() == '\n' || printed.back() == '\r')) {
        printed.pop_back();
    }
    REQUIRE(std::filesystem::equivalent(printed, dir.path()));
}

TEST_CASE("a missing program or working directory is an error, not a crash", "[process]") {
    const std::vector<std::string> none;
    REQUIRE_FALSE(cup::platform::run_command(here(), "cup-no-such-program-xyz", none).has_value());
    REQUIRE_FALSE(
        cup::platform::capture_command(here(), "cup-no-such-program-xyz", none).has_value());

    const TempDir dir;
    const auto missing = dir.path() / "does-not-exist";
    REQUIRE_FALSE(
        cup::platform::run_command(missing, kHelper, std::vector<std::string>{"exit", "0"})
            .has_value());
}

TEST_CASE("cache_dir prefers XDG_CACHE_HOME", "[env]") {
    const TempDir dir;
    const ScopedEnv xdg("XDG_CACHE_HOME", dir.path().string());
    REQUIRE(cup::platform::cache_dir() == dir.path() / "cup");
}

TEST_CASE("home_dir and cache_dir fall back to HOME", "[env]") {
    const TempDir dir;
    const ScopedEnv xdg("XDG_CACHE_HOME", std::nullopt);
    const ScopedEnv local("LOCALAPPDATA", std::nullopt);
    const ScopedEnv home("HOME", dir.path().string());
    REQUIRE(cup::platform::home_dir() == dir.path());
    REQUIRE(cup::platform::cache_dir() == dir.path() / ".cache" / "cup");
}

#ifdef _WIN32
TEST_CASE("on Windows, home_dir falls back to USERPROFILE and cache_dir to LOCALAPPDATA",
          "[env][windows]") {
    const TempDir dir;
    const ScopedEnv xdg("XDG_CACHE_HOME", std::nullopt);
    const ScopedEnv home("HOME", std::nullopt);
    const ScopedEnv profile("USERPROFILE", dir.path().string());
    const ScopedEnv local("LOCALAPPDATA", (dir.path() / "AppData").string());
    REQUIRE(cup::platform::home_dir() == dir.path());
    REQUIRE(cup::platform::cache_dir() == dir.path() / "AppData" / "cup");
}

TEST_CASE("on Windows, executables carry .exe", "[env][windows]") {
    REQUIRE(cup::platform::kExeSuffix == ".exe");
}
#else
TEST_CASE("on POSIX, executables carry no suffix", "[env]") {
    REQUIRE(cup::platform::kExeSuffix.empty());
}
#endif
