module;
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
module cup.ui;

// Fully qualified throughout rather than `using namespace ftxui`: ftxui::text
// and ftxui::color (element/decorator constructors) would otherwise collide
// with cup::ui::text and cup::ui::color (this file is part of module cup.ui,
// so both are in scope), and unqualified lookup silently prefers cup::ui's.
namespace cup::ui::detail {
namespace {

// Same 256-color palette indices as cup::ui::kCyan/kGreen/kGrey/kRed
// (Color.cppm), duplicated as plain ints rather than parsed out of those
// ANSI SGR strings, so the interactive widgets share cup's palette without a
// fragile string-parsing dependency on cup::ui::color()'s raw code format.
constexpr std::uint8_t kCyanIdx = 81;
constexpr std::uint8_t kRedIdx = 203;

[[nodiscard]] ftxui::Color cup_color(std::uint8_t idx) {
    return {static_cast<ftxui::Color::Palette256>(idx)};
}

}

std::expected<std::string, error::Error> select_interactive_impl(
    std::string_view question, std::span<const std::string> options, std::size_t initial_cursor) {
    std::vector<std::string> entries(options.begin(), options.end());
    int selected = static_cast<int>(initial_cursor);
    bool aborted = false;

    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    ftxui::MenuOption menu_option;
    menu_option.on_enter = screen.ExitLoopClosure();
    menu_option.entries_option.transform = [](const ftxui::EntryState& s) {
        ftxui::Element label = ftxui::text((s.state ? "> " : "  ") + s.label);
        return s.state ? (label | ftxui::bold | ftxui::color(cup_color(kCyanIdx))) : label;
    };

    auto menu = ftxui::Menu(&entries, &selected, menu_option);

    auto with_header = ftxui::Renderer(menu, [&] {
        return ftxui::vbox(ftxui::Elements{
            ftxui::hbox(ftxui::Elements{
                ftxui::text("? ") | ftxui::bold | ftxui::color(cup_color(kCyanIdx)),
                ftxui::text(std::string(question)),
                ftxui::text("  (up/down, enter)") | ftxui::dim,
            }),
            menu->Render(),
        });
    });

    auto component = ftxui::CatchEvent(with_header, [&](const ftxui::Event& event) {
        if (event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD) {
            aborted = true;
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(component);

    if (aborted) {
        return std::unexpected(error::abort_error());
    }
    return entries[static_cast<std::size_t>(selected)];
}

std::expected<std::string, error::Error> text_interactive_impl(std::string_view question,
                                                                std::string_view def,
                                                                const Validator& validate) {
    std::string content;
    std::string error_message;
    bool aborted = false;

    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    ftxui::InputOption input_option;
    input_option.multiline = false;
    input_option.placeholder = std::string(def);
    input_option.on_enter = [&] {
        const std::string answer = content.empty() ? std::string(def) : content;
        if (validate) {
            if (auto checked = validate(answer); !checked.has_value()) {
                error_message = checked.error().message();
                return;
            }
        }
        error_message.clear();
        screen.Exit();
    };

    auto input = ftxui::Input(&content, input_option);

    auto with_header = ftxui::Renderer(input, [&] {
        ftxui::Elements lines{
            ftxui::hbox(ftxui::Elements{
                ftxui::text("? ") | ftxui::bold | ftxui::color(cup_color(kCyanIdx)),
                ftxui::text(std::string(question)),
                ftxui::text(" "),
                input->Render(),
            }),
        };
        if (!error_message.empty()) {
            lines.push_back(ftxui::text("  " + error_message) | ftxui::color(cup_color(kRedIdx)));
        }
        return ftxui::vbox(std::move(lines));
    });

    auto component = ftxui::CatchEvent(with_header, [&](const ftxui::Event& event) {
        if (event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD) {
            aborted = true;
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(component);

    if (aborted) {
        return std::unexpected(error::abort_error());
    }
    return content.empty() ? std::string(def) : content;
}

}
