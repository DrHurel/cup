// Port of internal/ui/ui_test.go and internal/ui/select_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <expected>
// cup.ui exports Validator as a std::function alias, but a module only re-exports
// declarations from its global module fragment — instantiating the template here
// needs the definition, so this consumer includes <functional> itself.
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import cup.ui;
import cup.error;

namespace {

using cup::error::Error;

// ScopedColor forces colouring on or off for one test and restores it after, so
// the suite does not depend on whether stdout happens to be a terminal.
class ScopedColor {
public:
    explicit ScopedColor(bool enabled) : previous_(cup::ui::set_use_color(enabled)) {}
    ~ScopedColor() { cup::ui::set_use_color(previous_); }

private:
    bool previous_;
};

}  // namespace

// Go: TestColor
TEST_CASE("color honours the colour setting", "[ui][color]") {
    SECTION("disabled leaves the text untouched") {
        const ScopedColor guard(false);
        REQUIRE(cup::ui::color(cup::ui::kCyan, "hi") == "hi");
    }
    SECTION("enabled wraps the text in escapes") {
        const ScopedColor guard(true);
        REQUIRE(cup::ui::color("1", "hi") == "\x1b[1mhi\x1b[0m");
    }
}

// Go: TestTextUsesDefaultOnEmpty
TEST_CASE("text falls back to the default on empty input", "[ui][text]") {
    std::istringstream in("\n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "fallback");
    REQUIRE(got.has_value());
    REQUIRE(*got == "fallback");
}

// Go: TestTextTrimsAndReturnsInput
TEST_CASE("text trims surrounding whitespace", "[ui][text]") {
    std::istringstream in("  hello  \n");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "def");
    REQUIRE(got.has_value());
    REQUIRE(*got == "hello");
}

// Go: TestTextRepeatsUntilValid
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

// Go: TestTextAbortsOnEOF
TEST_CASE("text aborts on EOF", "[ui][text]") {
    std::istringstream in("");  // immediate EOF, no data
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::text("name?", "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

// Go: TestConfirm
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

// Go: TestConfirmAbortsOnEOF
TEST_CASE("confirm aborts on EOF", "[ui][confirm]") {
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::confirm("ok?", false);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

// Go: TestStatusLinesDoNotPanic
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

// Go: TestIndexOf
TEST_CASE("index_of finds an option, or falls back to the first", "[ui][select]") {
    const std::vector<std::string> options{"a", "b", "c"};
    REQUIRE(cup::ui::detail::index_of(options, "a") == 0);
    REQUIRE(cup::ui::detail::index_of(options, "b") == 1);
    REQUIRE(cup::ui::detail::index_of(options, "c") == 2);
    REQUIRE(cup::ui::detail::index_of(options, "z") == 0);  // absent -> 0
    REQUIRE(cup::ui::detail::index_of(options, "") == 0);
}

// Go: TestIsUpKey
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
    // Right bytes, wrong length: a lone ESC is not an arrow press.
    REQUIRE_FALSE(cup::ui::detail::is_up_key(arrow, 1));
}

// Go: TestIsDownKey
TEST_CASE("is_down_key decodes j and the down-arrow sequence", "[ui][select][keys]") {
    const std::array<unsigned char, 3> vim{'j', 0, 0};
    const std::array<unsigned char, 3> arrow{27, '[', 'B'};
    REQUIRE(cup::ui::detail::is_down_key(vim, 1));
    REQUIRE(cup::ui::detail::is_down_key(arrow, 3));

    const std::array<unsigned char, 3> up_vim{'k', 0, 0};
    const std::array<unsigned char, 3> up_arrow{27, '[', 'A'};
    REQUIRE_FALSE(cup::ui::detail::is_down_key(up_vim, 1));
    REQUIRE_FALSE(cup::ui::detail::is_down_key(up_arrow, 3));
    REQUIRE_FALSE(cup::ui::detail::is_down_key(arrow, 1));  // wrong length
}

// Go: TestSelectNumbered
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

// Go: TestSelectNumberedAbort
TEST_CASE("select_numbered aborts on EOF", "[ui][select]") {
    const std::vector<std::string> options{"a", "b"};
    std::istringstream in("");
    const cup::ui::ScopedInput scoped(in);

    const auto got = cup::ui::detail::select_numbered("pick?", options);
    REQUIRE_FALSE(got.has_value());
    REQUIRE(cup::error::is_abort(got.error()));
}

// Go: TestSelectNoOptions
TEST_CASE("select_one rejects an empty option list", "[ui][select]") {
    const std::vector<std::string> none;
    const auto got = cup::ui::select_one("pick?", none, "");
    REQUIRE_FALSE(got.has_value());
    REQUIRE(got.error().message() == "no options to choose from");
}
