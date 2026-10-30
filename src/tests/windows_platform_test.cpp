#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "TempDir.hpp"
#include "TestCompat.hpp"

import core;
import windows;

// windows's own suite — built only on Windows (see
// CMakeLists.txt), like the backend it tests. process_test.cpp covers what
// both backends share through the portable API; this covers what only the
// Windows one does: its services-file wiring, the %USERPROFILE% and
// %LOCALAPPDATA% fallbacks, and CreateProcessW's command-line quoting.

namespace {

using cup::test::TempDir;

// Sets (or, with nullopt, unsets) an environment variable for the test's
// lifetime. Duplicated per file, as log_test.cpp's ScopedEnv explains.
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

TEST_CASE("the Windows services file injects the Windows backend", "[windows][services]") {
    REQUIRE(core::exe_suffix() == ".exe");
    REQUIRE(core::default_shell() == "powershell");
}

TEST_CASE("home_dir falls back to USERPROFILE and cache_dir to LOCALAPPDATA", "[windows][env]") {
    const TempDir dir;
    const ScopedEnv xdg("XDG_CACHE_HOME", std::nullopt);
    const ScopedEnv home("HOME", std::nullopt);
    const ScopedEnv profile("USERPROFILE", dir.path().string());
    const ScopedEnv local("LOCALAPPDATA", (dir.path() / "AppData").string());
    REQUIRE(core::home_dir() == dir.path());
    REQUIRE(core::cache_dir() == dir.path() / "AppData" / "cup");
}

TEST_CASE("$HOME (MSYS2, Git Bash) wins over USERPROFILE", "[windows][env]") {
    const TempDir dir;
    const ScopedEnv home("HOME", (dir.path() / "msys").string());
    const ScopedEnv profile("USERPROFILE", dir.path().string());
    REQUIRE(core::home_dir() == dir.path() / "msys");
}

TEST_CASE("quote_arg follows CommandLineToArgvW's rules", "[windows][process]") {
    using windows::quote_arg;
    REQUIRE(quote_arg(L"plain") == L"plain");
    REQUIRE(quote_arg(L"") == L"\"\"");
    REQUIRE(quote_arg(L"with space") == L"\"with space\"");
    REQUIRE(quote_arg(LR"(say "hi")") == LR"("say \"hi\"")");
    // Backslashes are literal unless they precede a quote...
    REQUIRE(quote_arg(LR"(C:\dir\file)") == LR"(C:\dir\file)");
    // ...including the closing one cup adds.
    REQUIRE(quote_arg(LR"(C:\my dir\)") == LR"("C:\my dir\\")");
    REQUIRE(quote_arg(LR"(a\"b)") == LR"("a\\\"b")");
}

TEST_CASE("enter_raw_mode reports failure off a console", "[windows][terminal]") {
    // ctest runs the suite with redirected stdio: fd 1 is a pipe or file,
    // which has no console mode to change.
    REQUIRE_FALSE(core::enter_raw_mode(core::kStdoutFd).has_value());
}
