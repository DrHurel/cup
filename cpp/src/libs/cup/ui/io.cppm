module;
#include <unistd.h>

#include <iostream>
#include <print>
#include <span>
#include <string>
#include <string_view>
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

// emit_error_line writes s to stderr, so diagnostics survive a redirect of stdout.
void emit_error_line(std::string_view s) { std::println(stderr, "{}", s); }

// flush_output pushes buffered output out before a read, so a prompt is visible
// before cup blocks waiting for the answer.
void flush_output() { std::cout.flush(); }

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
    explicit ScopedInput(std::istream& in) : previous_(detail::current_input()) {
        detail::current_input() = &in;
    }
    ScopedInput(const ScopedInput&) = delete;
    ScopedInput& operator=(const ScopedInput&) = delete;
    ~ScopedInput() { detail::current_input() = previous_; }

private:
    std::istream* previous_;
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
