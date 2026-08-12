
#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import cup.ui;
import cup.error;
import cup.probe;
import cup.platform;

namespace {

using cup::error::Error;

class ScopedColor {
public:
    explicit ScopedColor(bool enabled) : previous_(cup::ui::set_use_color(enabled)) {}
    ~ScopedColor() { cup::ui::set_use_color(previous_); }

private:
    bool previous_;
};

class ScopedStdin {
public:
    explicit ScopedStdin(std::string_view keys) {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::write(fds[1], keys.data(), keys.size()) == static_cast<ssize_t>(keys.size()));
        REQUIRE(::close(fds[1]) == 0);
        REQUIRE(::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO);
        REQUIRE(::close(fds[0]) == 0);
    }

    ScopedStdin(const ScopedStdin&) = delete;
    ScopedStdin& operator=(const ScopedStdin&) = delete;

    ~ScopedStdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
    }

private:
    int saved_ = -1;
};

class ScopedTerminalStdin {
public:
    explicit ScopedTerminalStdin(std::string_view keys) : keys_(keys) {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master_ >= 0);
        REQUIRE(::grantpt(master_) == 0);
        REQUIRE(::unlockpt(master_) == 0);
        const char* const name = ::ptsname(master_);
        REQUIRE(name != nullptr);
        slave_ = ::open(name, O_RDWR | O_NOCTTY);
        REQUIRE(slave_ >= 0);

        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::dup2(slave_, STDIN_FILENO) == STDIN_FILENO);

        typist_ = std::thread([this] { type(); });
    }

    ScopedTerminalStdin(const ScopedTerminalStdin&) = delete;
    ScopedTerminalStdin& operator=(const ScopedTerminalStdin&) = delete;

    ~ScopedTerminalStdin() {
        typist_.join();
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
        ::close(slave_);
        if (master_ >= 0) {
            ::close(master_);
        }
    }

    [[nodiscard]] bool typed_in_raw_mode() const { return raw_seen_; }

private:
    void type() {
        for (int waited = 0; waited < 2000 && !raw_seen_; ++waited) {
            termios now{};
            if (::tcgetattr(slave_, &now) == 0 &&
                (now.c_lflag & static_cast<tcflag_t>(ICANON)) == 0) {
                raw_seen_ = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        if (::write(master_, keys_.data(), keys_.size()) !=
            static_cast<ssize_t>(keys_.size())) {
            ::close(master_);
            master_ = -1;
        }
    }

    std::string keys_;
    std::atomic<bool> raw_seen_ = false;
    std::thread typist_;
    int master_ = -1;
    int slave_ = -1;
    int saved_ = -1;
};

class CapturedStdout {
public:
    CapturedStdout() {
        file_ = std::tmpfile();
        REQUIRE(file_ != nullptr);
        saved_ = ::dup(STDOUT_FILENO);
        REQUIRE(saved_ >= 0);
        cup::ui::flush_output();
        REQUIRE(::dup2(::fileno(file_), STDOUT_FILENO) == STDOUT_FILENO);
    }

    CapturedStdout(const CapturedStdout&) = delete;
    CapturedStdout& operator=(const CapturedStdout&) = delete;

    ~CapturedStdout() {
        stop();
        std::fclose(file_);
    }

    void stop() {
        if (saved_ < 0) {
            return;
        }
        cup::ui::flush_output();
        ::dup2(saved_, STDOUT_FILENO);
        ::close(saved_);
        saved_ = -1;
    }

    [[nodiscard]] std::string str() {
        stop();
        std::rewind(file_);
        std::string out;
        std::array<char, 512> chunk{};
        while (const std::size_t n = std::fread(chunk.data(), 1, chunk.size(), file_)) {
            out.append(chunk.data(), n);
        }
        return out;
    }

private:
    std::FILE* file_ = nullptr;
    int saved_ = -1;
};

constexpr std::size_t kFrameBytes = 3;

[[nodiscard]] std::string frame(std::string_view press) {
    std::string padded(press);
    padded.resize(kFrameBytes, '\0');
    return padded;
}

constexpr std::string_view kUp = "\x{1b}[A";
constexpr std::string_view kDown = "\x{1b}[B";
constexpr std::string_view kVimUp = "k";
constexpr std::string_view kVimDown = "j";
constexpr std::string_view kEnter = "\r";
constexpr std::string_view kLineFeed = "\n";
constexpr std::string_view kCtrlC = "\x{03}";
constexpr std::string_view kCtrlD = "\x{04}";

}

TEST_CASE("color honours the colour setting", "[ui][color]") {
    SECTION("disabled leaves the text untouched") {
        const ScopedColor guard(false);
        REQUIRE(cup::ui::color(cup::ui::kCyan, "hi") == "hi");
    }
    SECTION("enabled wraps the text in escapes") {
        const ScopedColor guard(true);
        REQUIRE(cup::ui::color("1", "hi") == "\x{1b}[1mhi\x{1b}[0m");
    }
}

TEST_CASE("text falls back to the default on empty input", "[ui][text]") {
    std::istringstream in("\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "fallback");
    REQUIRE(got.has_value());
    REQUIRE(*got == "fallback");
}

TEST_CASE("text trims surrounding whitespace", "[ui][text]") {
    std::istringstream in("  hello  \n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "def");
    REQUIRE(got.has_value());
    REQUIRE(*got == "hello");
}

TEST_CASE("text repeats until the value validates", "[ui][text]") {
    std::istringstream in("bad\ngood\n");
    const cup::ui::ScopedInput scoped(in);

    const cup::ui::Validator only_good =
        [](std::string_view s) -> std::expected<void, Error> {
        if (s != "good") {
            return std::unexpected(Error("must be good"));
        }
        return {};
    };

    const auto got = cup::ui::text("name?", "", only_good);
    REQUIRE(got.has_value());
    REQUIRE(*got == "good");
}

TEST_CASE("text aborts on EOF", "[ui][text]") {
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

TEST_CASE("confirm parses answers and honours its default", "[ui][confirm]") {
    struct Case {
        const char* input;
        bool def;
        bool want;
        const char* why;
    };
    const std::array<Case, 8> cases{{
        {"y\n", false, true, "y"},
        {"yes\n", false, true, "yes"},
        {"n\n", true, false, "n"},
        {"no\n", true, false, "no"},
        {"Y\n", false, true, "case-insensitive"},
        {"\n", true, true, "empty takes the true default"},
        {"\n", false, false, "empty takes the false default"},
        {"maybe\ny\n", false, true, "unrecognised answer repeats"},
    }};

    for (const auto& c : cases) {
        INFO(c.why);
        std::istringstream in(c.input);
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::confirm("ok?", c.def);
        REQUIRE(got.has_value());
        REQUIRE(*got == c.want);
    }
}

TEST_CASE("confirm aborts on EOF", "[ui][confirm]") {
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::confirm("ok?", false);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

TEST_CASE("the status helpers emit without throwing", "[ui][status]") {
    REQUIRE_NOTHROW(cup::ui::running("running message"));
    REQUIRE_NOTHROW(cup::ui::wrote("wrote message"));
    REQUIRE_NOTHROW(cup::ui::updated("updated message"));
    REQUIRE_NOTHROW(cup::ui::skipped("skipped message"));
    REQUIRE_NOTHROW(cup::ui::removed("removed message"));
    REQUIRE_NOTHROW(cup::ui::next("next message"));
    REQUIRE_NOTHROW(cup::ui::accent("accent message"));
    REQUIRE_NOTHROW(cup::ui::success("success message"));
    REQUIRE_NOTHROW(cup::ui::err("err message"));
}

TEST_CASE("index_of finds an option, or falls back to the first", "[ui][select]") {
    const std::vector<std::string> options{"a", "b", "c"};
    REQUIRE(cup::ui::detail::index_of(options, "a") == 0);
    REQUIRE(cup::ui::detail::index_of(options, "b") == 1);
    REQUIRE(cup::ui::detail::index_of(options, "c") == 2);
    REQUIRE(cup::ui::detail::index_of(options, "z") == 0);
    REQUIRE(cup::ui::detail::index_of(options, "") == 0);
}

TEST_CASE("is_up_key decodes k and the up-arrow sequence", "[ui][select][keys]") {
    const std::array<unsigned char, 3> vim{'k', 0, 0};
    const std::array<unsigned char, 3> arrow{27, '[', 'A'};
    REQUIRE(cup::ui::detail::is_up_key(vim, 1));
    REQUIRE(cup::ui::detail::is_up_key(arrow, 3));

    const std::array<unsigned char, 3> down_vim{'j', 0, 0};
    const std::array<unsigned char, 3> down_arrow{27, '[', 'B'};
    const std::array<unsigned char, 3> other{'x', 0, 0};
    REQUIRE_FALSE(cup::ui::detail::is_up_key(down_vim, 1));
    REQUIRE_FALSE(cup::ui::detail::is_up_key(down_arrow, 3));
    REQUIRE_FALSE(cup::ui::detail::is_up_key(other, 1));
    REQUIRE_FALSE(cup::ui::detail::is_up_key(arrow, 1));
    REQUIRE_FALSE(cup::ui::detail::is_up_key({}, 0));
}

TEST_CASE("is_down_key decodes j and the down-arrow sequence", "[ui][select][keys]") {
    const std::array<unsigned char, 3> vim{'j', 0, 0};
    const std::array<unsigned char, 3> arrow{27, '[', 'B'};
    REQUIRE(cup::ui::detail::is_down_key(vim, 1));
    REQUIRE(cup::ui::detail::is_down_key(arrow, 3));

    const std::array<unsigned char, 3> up_vim{'k', 0, 0};
    const std::array<unsigned char, 3> up_arrow{27, '[', 'A'};
    REQUIRE_FALSE(cup::ui::detail::is_down_key(up_vim, 1));
    REQUIRE_FALSE(cup::ui::detail::is_down_key(up_arrow, 3));
    REQUIRE_FALSE(cup::ui::detail::is_down_key(arrow, 1));
    REQUIRE_FALSE(cup::ui::detail::is_down_key({}, 0));
}

TEST_CASE("select_interactive walks the list and returns the highlighted option",
          "[ui][select][keys]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    const struct Case {
        std::string keys;
        std::size_t start;
        const char* want;
        const char* why;
    } cases[]{
        {frame(kEnter), 0, "red", "enter takes the option under the cursor"},
        {frame(kDown) + frame(kEnter), 0, "green", "down moves one"},
        {frame(kDown) + frame(kDown) + frame(kEnter), 0, "blue", "and again"},
        {frame(kUp) + frame(kEnter), 0, "blue", "up from the first wraps to the last"},
        {frame(kDown) + frame(kEnter), 2, "red", "down from the last wraps to the first"},
        {frame(kVimDown) + frame(kEnter), 0, "green", "j moves down"},
        {frame(kVimUp) + frame(kEnter), 1, "red", "k moves up"},
        {frame("x") + frame(kEnter), 1, "green", "a key with no meaning is ignored"},
        {frame(kLineFeed), 1, "green", "a terminal sending LF rather than CR commits too"},
        {frame(kDown) + frame(kUp) + frame(kEnter), 1, "green", "down then up is where it began"},
    };

    for (const auto& c : cases) {
        INFO(c.why);
        const ScopedStdin keys(c.keys);
        CapturedStdout painted;

        const auto got = cup::ui::detail::select_interactive("pick?", options, c.start);
        painted.stop();

        REQUIRE(got.has_value());
        REQUIRE(*got == c.want);
    }
}

TEST_CASE("select_interactive aborts on Ctrl+C, Ctrl+D and a closed input",
          "[ui][select][keys]") {
    const std::vector<std::string> options{"red", "green"};

    const struct Case {
        std::string keys;
        const char* why;
    } cases[]{
        {frame(kCtrlC), "Ctrl+C"},
        {frame(kCtrlD), "Ctrl+D"},
        {std::string(), "the input ended with no key pressed"},
    };

    for (const auto& c : cases) {
        INFO(c.why);
        const ScopedStdin keys(c.keys);
        CapturedStdout painted;

        const auto got = cup::ui::detail::select_interactive("pick?", options, 0);
        painted.stop();

        REQUIRE_FALSE(got.has_value());
        REQUIRE(cup::error::is_abort(got.error()));
    }
}

TEST_CASE("select_interactive redraws in place and marks the pick", "[ui][select][render]") {
    const ScopedColor colour(true);
    const std::vector<std::string> options{"red", "green", "blue"};
    const ScopedStdin keys(frame(kDown) + frame(kEnter));

    CapturedStdout capture;
    const auto got = cup::ui::detail::select_interactive("pick?", options, 0);
    const std::string painted = capture.str();

    REQUIRE(got.has_value());
    REQUIRE(*got == "green");
    REQUIRE(painted ==
            "\x{1b}[38;5;81;1m?\x{1b}[0m pick? \x{1b}[38;5;244m(up/down, enter)\x{1b}[0m\r\n"
            "\x{1b}[38;5;81;1m> \x{1b}[0m\x{1b}[38;5;81;1mred\x{1b}[0m\r\n"
            "  green\r\n"
            "  blue\r\n"
            "\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K"
            "  red\r\n"
            "\x{1b}[38;5;81;1m> \x{1b}[0m\x{1b}[38;5;81;1mgreen\x{1b}[0m\r\n"
            "  blue\r\n"
            "\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K\x{1b}[1A\x{1b}[2K"
            "  \x{1b}[38;5;244mred\x{1b}[0m\r\n"
            "\x{1b}[38;5;77;1m> \x{1b}[0m\x{1b}[38;5;77;1mgreen\x{1b}[0m\r\n"
            "  \x{1b}[38;5;244mblue\x{1b}[0m\r\n");
}

TEST_CASE("select_numbered picks by index and repeats when out of range", "[ui][select]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    SECTION("a valid choice returns the matching option") {
        std::istringstream in("2\n");
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::detail::select_numbered("pick?", options);
        REQUIRE(got.has_value());
        REQUIRE(*got == "green");
    }

    SECTION("out of range, then valid") {
        std::istringstream in("9\n1\n");
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::detail::select_numbered("pick?", options);
        REQUIRE(got.has_value());
        REQUIRE(*got == "red");
    }
}

TEST_CASE("select_numbered puts the question again for an answer it cannot use",
          "[ui][select]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    std::istringstream in(
        "abc\n"
        "1x\n"
        "0\n"
        "4\n"
        "2\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::detail::select_numbered("pick?", options);
    REQUIRE(got.has_value());
    REQUIRE(*got == "green");
}

TEST_CASE("select_numbered aborts on EOF", "[ui][select]") {
    const std::vector<std::string> options{"a", "b"};
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::detail::select_numbered("pick?", options);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

TEST_CASE("select_one rejects an empty option list", "[ui][select]") {
    const std::vector<std::string> none;
    const auto got = cup::ui::select_one("pick?", none, "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(got.error().message() == "no options to choose from");
}

TEST_CASE("select_one falls back to the numbered list off a terminal", "[ui][select]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    const ScopedStdin not_a_terminal("");
    REQUIRE_FALSE(cup::platform::is_tty(cup::platform::kStdinFd));

    std::istringstream in("3\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::select_one("pick?", options, "green");
    REQUIRE(got.has_value());
    REQUIRE(*got == "blue");
}

TEST_CASE("select_one drives the arrow-key menu on a terminal", "[ui][select][keys]") {
    const std::vector<std::string> options{"red", "green", "blue"};

    ScopedTerminalStdin terminal("\r");
    REQUIRE(cup::platform::is_tty(cup::platform::kStdinFd));

    CapturedStdout capture;
    const auto got = cup::ui::select_one("pick?", options, "blue");
    const std::string painted = capture.str();

    REQUIRE(terminal.typed_in_raw_mode());
    REQUIRE(got.has_value());
    REQUIRE(*got == "blue");
    REQUIRE(painted.contains("(up/down, enter)"));
}

TEST_CASE("errors compare by kind and message", "[error]") {
    REQUIRE(cup::error::abort_error() == cup::error::abort_error());
    REQUIRE(cup::error::abort_error().kind() == Error::Kind::Abort);
    REQUIRE(cup::error::abort_error().message() == "aborted");

    REQUIRE(Error("aborted").kind() == Error::Kind::General);
    REQUIRE_FALSE(Error("aborted") == cup::error::abort_error());
    REQUIRE_FALSE(cup::error::is_abort(Error("aborted")));

    REQUIRE(Error("boom") == Error("boom"));
    REQUIRE_FALSE(Error("boom") == Error("bang"));
    REQUIRE(Error{}.message().empty());
    REQUIRE_FALSE(cup::error::is_abort(Error{}));
}

TEST_CASE("a scaffolded library is importable", "[scaffold][modules]") {
    REQUIRE_NOTHROW(cup::probe::Probe());
}
