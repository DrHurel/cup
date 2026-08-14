
#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

// Points the real stdin fd at a pipe, so cup.platform::is_tty(kStdinFd) sees
// "not a terminal" regardless of how the test binary was invoked (ctest's
// own stdin may or may not be a tty depending on how it is run). Content is
// irrelevant here -- read_line() reads from ScopedInput's istream, never
// from this fd directly; this only has to exist and fail isatty().
class ScopedStdin {
public:
    ScopedStdin() : ScopedStdin("") {}
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

// Points the real stdin fd at a pty slave, so is_tty(kStdinFd) is true and
// enter_raw_mode(kStdinFd) actually succeeds -- the two things
// cup::ui::detail::can_use_interactive gates on. Unlike the old
// ScopedTerminalStdin this never types anything into the pty: the
// interactive seam is stubbed in these tests (see ScopedOverride below), so
// nothing ever reads from it.
class ScopedPtyStdin {
public:
    ScopedPtyStdin() {
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
    }

    ScopedPtyStdin(const ScopedPtyStdin&) = delete;
    ScopedPtyStdin& operator=(const ScopedPtyStdin&) = delete;

    ~ScopedPtyStdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
        ::close(slave_);
        ::close(master_);
    }

private:
    int master_ = -1;
    int slave_ = -1;
    int saved_ = -1;
};

// Restores a mutable global (an overridable hook) to its prior value when the
// test ends, mirroring cmd_test.cpp's / releases_test.cpp's ScopedOverride.
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
    const ScopedStdin not_a_terminal;
    std::istringstream in("\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "fallback");
    REQUIRE(got.has_value());
    REQUIRE(*got == "fallback");
}

TEST_CASE("text trims surrounding whitespace", "[ui][text]") {
    const ScopedStdin not_a_terminal;
    std::istringstream in("  hello  \n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "def");
    REQUIRE(got.has_value());
    REQUIRE(*got == "hello");
}

TEST_CASE("text repeats until the value validates", "[ui][text]") {
    const ScopedStdin not_a_terminal;
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
    const ScopedStdin not_a_terminal;
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

TEST_CASE("text calls the interactive seam on a terminal", "[ui][text]") {
    const ScopedPtyStdin pty;
    REQUIRE(cup::platform::is_tty(cup::platform::kStdinFd));

    std::string seen_question;
    std::string seen_def;
    const ScopedOverride guard(
        cup::ui::text_interactive_func(),
        cup::ui::TextInteractiveFunc{[&](std::string_view q, std::string_view def,
                                         const cup::ui::Validator&)
                                          -> std::expected<std::string, Error> {
            seen_question = q;
            seen_def = def;
            return std::string("typed");
        }});

    const auto got = cup::ui::text("name?", "fallback");
    REQUIRE(got.has_value());
    REQUIRE(*got == "typed");
    REQUIRE(seen_question == "name?");
    REQUIRE(seen_def == "fallback");
}

TEST_CASE("text propagates the interactive seam's error", "[ui][text]") {
    const ScopedPtyStdin pty;
    const ScopedOverride guard(
        cup::ui::text_interactive_func(),
        cup::ui::TextInteractiveFunc{[](std::string_view, std::string_view,
                                        const cup::ui::Validator&)
                                         -> std::expected<std::string, Error> {
            return std::unexpected(cup::error::abort_error());
        }});

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
        const ScopedStdin not_a_terminal;
        std::istringstream in(c.input);
        const cup::ui::ScopedInput scoped(in);

        const auto got = cup::ui::confirm("ok?", c.def);
        REQUIRE(got.has_value());
        REQUIRE(*got == c.want);
    }
}

TEST_CASE("confirm aborts on EOF", "[ui][confirm]") {
    const ScopedStdin not_a_terminal;
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::confirm("ok?", false);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

TEST_CASE("confirm calls the same interactive seam as select_one", "[ui][confirm][select]") {
    const ScopedPtyStdin pty;
    std::vector<std::string> seen_options;
    std::size_t seen_cursor = 0;
    const ScopedOverride guard(
        cup::ui::select_interactive_func(),
        cup::ui::SelectInteractiveFunc{[&](std::string_view, std::span<const std::string> opts,
                                           std::size_t cursor)
                                            -> std::expected<std::string, Error> {
            seen_options.assign(opts.begin(), opts.end());
            seen_cursor = cursor;
            return std::string("Yes");
        }});

    const auto got = cup::ui::confirm("ok?", false);
    REQUIRE(got.has_value());
    REQUIRE(*got);
    REQUIRE(seen_options == std::vector<std::string>{"Yes", "No"});
    REQUIRE(seen_cursor == 1);
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

    const ScopedStdin not_a_terminal;
    REQUIRE_FALSE(cup::platform::is_tty(cup::platform::kStdinFd));

    std::istringstream in("3\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::select_one("pick?", options, "green");
    REQUIRE(got.has_value());
    REQUIRE(*got == "blue");
}

TEST_CASE("select_one calls the interactive seam on a terminal", "[ui][select]") {
    const ScopedPtyStdin pty;
    REQUIRE(cup::platform::is_tty(cup::platform::kStdinFd));

    std::string seen_question;
    std::vector<std::string> seen_options;
    std::size_t seen_cursor = 0;
    const ScopedOverride guard(
        cup::ui::select_interactive_func(),
        cup::ui::SelectInteractiveFunc{[&](std::string_view q, std::span<const std::string> opts,
                                           std::size_t cursor)
                                            -> std::expected<std::string, Error> {
            seen_question = q;
            seen_options.assign(opts.begin(), opts.end());
            seen_cursor = cursor;
            return std::string("blue");
        }});

    const std::vector<std::string> options{"red", "green", "blue"};
    const auto got = cup::ui::select_one("pick?", options, "green");
    REQUIRE(got.has_value());
    REQUIRE(*got == "blue");
    REQUIRE(seen_question == "pick?");
    REQUIRE(seen_options == options);
    REQUIRE(seen_cursor == 1);
}

TEST_CASE("select_one propagates the interactive seam's error", "[ui][select]") {
    const ScopedPtyStdin pty;
    const ScopedOverride guard(
        cup::ui::select_interactive_func(),
        cup::ui::SelectInteractiveFunc{[](std::string_view, std::span<const std::string>,
                                          std::size_t) -> std::expected<std::string, Error> {
            return std::unexpected(cup::error::abort_error());
        }});

    const std::vector<std::string> options{"a", "b"};
    const auto got = cup::ui::select_one("pick?", options, "a");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
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
