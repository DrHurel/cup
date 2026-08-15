#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A plain (non-module) header, included by Interactive.cpp and directly by
// ui_test.cpp -- not folded into a .cppm partition, because doing so would
// put FTXUI's headers in cup.ui's BMI (see Interactive.cpp's own note).
//
// Fully inline rather than declared-here/defined-in-Interactive.cpp: a
// declaration that's later defined inside `module cup.ui;`'s purview gets
// module linkage from GCC 15, which a plain #include in a non-modular TU
// like ui_test.cpp can't resolve against ("undefined reference", even
// though both sides agree on the signature). Inline definitions sidestep
// cross-TU linkage entirely -- each includer compiles its own ODR-identical
// copy, same as any other header-only helper.
//
// Splitting the component-tree construction out from the code that actually
// drives it via ftxui::ScreenInteractive::Loop() is what makes it testable:
// a test can build one of these components and feed it synthetic
// ftxui::Event objects directly, without a real terminal -- only Loop()
// itself needs one, and that's the one piece this deliberately leaves
// untested here (see ui_test.cpp for what's covered).
namespace cup::ui::detail {

namespace ftxui_style {

// Same 256-color palette indices as cup::ui::kCyan/kRed (Color.cppm),
// duplicated as plain ints rather than parsed out of those ANSI SGR
// strings, so the interactive widgets share cup's palette without a
// fragile string-parsing dependency on cup::ui::color()'s raw code format.
inline constexpr std::uint8_t kCyanIdx = 81;
inline constexpr std::uint8_t kRedIdx = 203;

[[nodiscard]] inline ftxui::Color cup_color(std::uint8_t idx) {
    return {static_cast<ftxui::Color::Palette256>(idx)};
}

}

struct SelectState {
    std::vector<std::string> entries;
    int selected = 0;
    bool aborted = false;
};

[[nodiscard]] inline ftxui::Component make_select_component(SelectState& state,
                                                             std::string_view question,
                                                             ftxui::ScreenInteractive& screen) {
    using namespace ftxui_style;

    ftxui::MenuOption menu_option;
    menu_option.on_enter = screen.ExitLoopClosure();
    // EntryState::state is a checkbox/radio toggle value -- always false for
    // a plain Menu entry, which is why this used to render with no
    // highlight at all. active is "the cursor is on this entry" (see
    // FTXUI's own DefaultOptionTransform in menu.cpp).
    menu_option.entries_option.transform = [](const ftxui::EntryState& s) {
        ftxui::Element label = ftxui::text((s.active ? "> " : "  ") + s.label);
        return s.active ? (label | ftxui::bold | ftxui::color(cup_color(kCyanIdx))) : label;
    };

    auto menu = ftxui::Menu(&state.entries, &state.selected, menu_option);

    // question copied by value: the caller's std::string_view outlives this
    // call, not the returned component (which the caller then screen.Loop()s
    // well after make_select_component itself has returned).
    auto with_header = ftxui::Renderer(menu, [menu, header = std::string(question)] {
        return ftxui::vbox(ftxui::Elements{
            ftxui::hbox(ftxui::Elements{
                ftxui::text("? ") | ftxui::bold | ftxui::color(cup_color(kCyanIdx)),
                ftxui::text(header),
                ftxui::text("  (up/down, enter)") | ftxui::dim,
            }),
            menu->Render(),
        });
    });

    return ftxui::CatchEvent(with_header, [&state, &screen](const ftxui::Event& event) {
        if (event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD) {
            state.aborted = true;
            screen.Exit();
            return true;
        }
        return false;
    });
}

struct TextState {
    std::string content;
    std::string error_message;
    bool aborted = false;
};

// validate returns the error message if `answer` is rejected, or nullopt if
// accepted -- std::string rather than cup::error::Error so this header (and
// the tests driving it) need not import cup.error.
[[nodiscard]] inline ftxui::Component make_text_component(
    TextState& state, std::string_view question, std::string_view def,
    ftxui::ScreenInteractive& screen,
    std::function<std::optional<std::string>(std::string_view)> validate) {
    using namespace ftxui_style;

    ftxui::InputOption input_option;
    input_option.multiline = false;
    input_option.placeholder = std::string(def);
    input_option.on_enter = [&state, &screen, def_str = std::string(def),
                             validate = std::move(validate)] {
        const std::string answer = state.content.empty() ? def_str : state.content;
        if (auto rejection = validate(answer); rejection.has_value()) {
            state.error_message = *std::move(rejection);
            return;
        }
        state.error_message.clear();
        screen.Exit();
    };

    auto input = ftxui::Input(&state.content, input_option);

    auto with_header = ftxui::Renderer(input, [&state, input, header = std::string(question)] {
        ftxui::Elements lines{
            ftxui::hbox(ftxui::Elements{
                ftxui::text("? ") | ftxui::bold | ftxui::color(cup_color(kCyanIdx)),
                ftxui::text(header),
                ftxui::text(" "),
                input->Render(),
            }),
        };
        if (!state.error_message.empty()) {
            lines.push_back(ftxui::text("  " + state.error_message) |
                            ftxui::color(cup_color(kRedIdx)));
        }
        return ftxui::vbox(std::move(lines));
    });

    return ftxui::CatchEvent(with_header, [&state, &screen](const ftxui::Event& event) {
        if (event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD) {
            state.aborted = true;
            screen.Exit();
            return true;
        }
        return false;
    });
}

}
