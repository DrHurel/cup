module;
#include <expected>
#include <span>
#include <string>
#include <string_view>

// In the global-module-fragment preamble, not after `module cup.ui;`: it's a
// plain header, and standard-library headers it drags in transitively
// (<functional> et al., via FTXUI's component.hpp) hit an internal compiler
// error in g++-15 when first included from inside a named module's purview
// rather than the global module fragment.
#include "Interactive.hpp"
module cup.ui;

namespace cup::ui::detail {

std::expected<std::string, error::Error> select_interactive_impl(
    std::string_view question, std::span<const std::string> options, std::size_t initial_cursor) {
    SelectState state{.entries = {options.begin(), options.end()},
                      .selected = static_cast<int>(initial_cursor)};

    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    auto component = make_select_component(state, question, screen);
    // Root cause of the Ctrl-C-kills-the-process bug: FTXUI's own
    // App::Internal::HandleTask always re-raises a real SIGINT once Loop()
    // returns after a Ctrl-C event -- even when a component's CatchEvent
    // already handled it (ours does, above in make_select_component) --
    // unless told not to. ForceHandleCtrlC(false) opts out of that
    // re-raise, since our own handling already produces the same
    // "aborted." + exit 1 outcome Ctrl-D gets.
    screen.ForceHandleCtrlC(false);
    screen.Loop(component);

    if (state.aborted) {
        return std::unexpected(error::abort_error());
    }
    return state.entries[static_cast<std::size_t>(state.selected)];
}

std::expected<std::string, error::Error> text_interactive_impl(std::string_view question,
                                                                std::string_view def,
                                                                const Validator& validate) {
    TextState state;
    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    auto component = make_text_component(
        state, question, def, screen, [&validate](std::string_view answer) -> std::optional<std::string> {
            if (!validate) {
                return std::nullopt;
            }
            auto checked = validate(answer);
            if (checked.has_value()) {
                return std::nullopt;
            }
            return checked.error().message();
        });
    // See select_interactive_impl's comment on ForceHandleCtrlC(false).
    screen.ForceHandleCtrlC(false);
    screen.Loop(component);

    if (state.aborted) {
        return std::unexpected(error::abort_error());
    }
    return state.content.empty() ? std::string(def) : state.content;
}

}
