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

export namespace cup::ui {

void emit(std::string_view s) { std::print("{}", s); }

void emit_line(std::string_view s) { std::println("{}", s); }

void emit_numbered_line(std::size_t n, std::string_view text) {
    std::println("  {}) {}", n, text);
}

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

void emit_error_line(std::string_view s) { std::println(stderr, "{}", s); }

void flush_output() { std::fflush(stdout); }

namespace detail {

std::istream*& current_input() {
    static std::istream* in = &std::cin;
    return in;
}

}

class ScopedInput {
public:
    explicit ScopedInput(std::istream& in) { detail::current_input() = &in; }
    ScopedInput(const ScopedInput&) = delete;
    ScopedInput& operator=(const ScopedInput&) = delete;
    ~ScopedInput() { detail::current_input() = previous_; }

private:
    std::istream* previous_ = detail::current_input();
};

bool read_line(std::string& line) {
    line.clear();
    std::getline(*detail::current_input(), line);
    return !line.empty() || !detail::current_input()->fail();
}

int read_key(std::span<unsigned char> buf) {
    return static_cast<int>(::read(STDIN_FILENO, buf.data(), buf.size()));
}

}
