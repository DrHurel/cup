module;
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <format>
#include <iostream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
export module cup.ui:io;

// :io is the only partition of cup.ui allowed to touch <print>, <iostream> or
// <format>, and that is a hard constraint rather than a style choice.
//
// GCC 14 miscompiles a module when two of its partitions carry any of those
// headers in their global module fragments: the primary interface unit then fails
// to read its own partition's BMI ("failed to read compiled module cluster N: Bad
// file data"), or the compiler gives up outright ("returning to the gate for a
// mechanical issue"). Light headers — <string>, <string_view>, <vector>,
// <expected> — can be repeated across partitions with no trouble; it is
// specifically the format/stream machinery that cannot.
//
// So every byte cup.ui reads or writes funnels through the handful of primitives
// below, and the other partitions build plain strings and hand them here.

export namespace cup::ui {

// emit writes s with no trailing newline — used for prompts, which leave the
// cursor on the same line as the question.
void emit(std::string_view s) { std::print("{}", s); }

// emit_line writes s followed by a newline.
void emit_line(std::string_view s) { std::println("{}", s); }

// emit_numbered_line writes "  <n>) <text>", the one line of cup's output that
// interpolates a number. The formatting lives here rather than at the call site
// because it has to: <format>'s machinery is as restricted to this partition as
// <print> is.
void emit_numbered_line(std::size_t n, std::string_view text) {
    std::println("  {}) {}", n, text);
}

// format_text is std::format made reachable from the rest of cup.ui. The other
// partitions assemble every prompt and menu line as a string before handing it to
// emit, and they cannot include <format> to do it — the one-partition rule above
// is the whole reason this module is split the way it is. Routing them through
// here keeps the header in :io while the call sites read as format strings rather
// than chains of `+`.
//
// These are deliberately three fixed-arity overloads rather than the obvious
// variadic template, and that shape is forced. A template would be *instantiated*
// in the calling partition, and GCC 14 gets the module linkage of <format>'s
// internals wrong when that happens:
//
//     error: 'std::__format::_Arg_store@cup.ui:io<...>::_Arg_store(_Tp& ...)'
//            is private within this context
//
// std::make_format_args is a friend of _Arg_store, but the friendship does not
// survive the entities being attached to :io, so every call site fails. Ordinary
// functions have their bodies compiled here instead, where the friendship still
// holds, and the partition boundary is never crossed mid-instantiation.
//
// The parameters stay std::format_string, so a placeholder that does not match its
// argument count is still a compile error at the call site — that check runs in
// basic_format_string's consteval constructor, which is unaffected. Every line
// cup.ui prints interpolates strings only, so string_view arguments are enough.
// The bodies call vformat rather than format because the parameters are lvalues by
// the time they are forwarded: std::format would re-deduce its _Args as reference
// types and stop matching the format_string spelled in the signature.
[[nodiscard]] std::string format_text(std::format_string<std::string_view> fmt,
                                      std::string_view a) {
    return std::vformat(fmt.get(), std::make_format_args(a));
}

[[nodiscard]] std::string format_text(std::format_string<std::string_view, std::string_view> fmt,
                                      std::string_view a, std::string_view b) {
    return std::vformat(fmt.get(), std::make_format_args(a, b));
}

[[nodiscard]] std::string format_text(
    std::format_string<std::string_view, std::string_view, std::string_view> fmt,
    std::string_view a, std::string_view b, std::string_view c) {
    return std::vformat(fmt.get(), std::make_format_args(a, b, c));
}

// emit_error_line writes s to stderr, so diagnostics survive a redirect of stdout.
void emit_error_line(std::string_view s) { std::println(stderr, "{}", s); }

// flush_output pushes buffered output out before a read, so a prompt is visible
// before cup blocks waiting for the answer.
void flush_output() { std::fflush(stdout); }

namespace detail {

// current_input is the stream the prompts read from. It defaults to std::cin and
// is redirectable, which is what lets cup be driven over a pipe — and what lets
// tests script a whole interactive session.
std::istream*& current_input() {
    static std::istream* in = &std::cin;
    return in;
}

}  // namespace detail

// ScopedInput redirects the prompts to another stream and restores the previous
// one when it goes out of scope. (Go: SetInput returning a restore func.)
class ScopedInput {
public:
    explicit ScopedInput(std::istream& in) { detail::current_input() = &in; }
    ScopedInput(const ScopedInput&) = delete;
    ScopedInput& operator=(const ScopedInput&) = delete;
    ~ScopedInput() { detail::current_input() = previous_; }

private:
    // Captured by the default member initializer, which runs before the
    // constructor body — so previous_ holds the stream that was installed on the
    // way in, not the one the body swaps to.
    std::istream* previous_ = detail::current_input();
};

// read_line reads one line, reporting false at end of input with nothing read. A
// final line without a trailing newline still counts as data, which is why the
// emptiness of the line — not the stream state alone — decides.
bool read_line(std::string& line) {
    line.clear();
    std::getline(*detail::current_input(), line);
    return !line.empty() || !detail::current_input()->fail();
}

// read_key reads raw bytes from the terminal for the arrow-key menu, returning how
// many were read (0 or less means the input ended). It bypasses current_input
// deliberately: raw-mode key handling only ever applies to a real terminal.
int read_key(std::span<unsigned char> buf) {
    return static_cast<int>(::read(STDIN_FILENO, buf.data(), buf.size()));
}

}  // namespace cup::ui
