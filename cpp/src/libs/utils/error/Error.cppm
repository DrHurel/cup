module;
#include <string>
#include <utility>
export module utils.error:error;

export namespace utils::error {

// Error is the E of every std::expected in cup — the C++ counterpart of Go's
// `error` interface. The Go implementation only ever carries a message (errors are
// built with fmt.Errorf and compared with errors.Is against a small set of
// sentinels), so a message plus an optional sentinel identity covers the whole
// surface that has to port.
class Error {
public:
    // Kind distinguishes the sentinel errors the Go code compares with errors.Is.
    // Everything else is General and is matched only by its message.
    enum class Kind {
        General,
        // The user aborted a prompt with Ctrl+C, Ctrl+D, or EOF. Commands turn
        // this into a clean "aborted." exit rather than an error report.
        // (Go: ui.ErrAbort.)
        Abort,
    };

    Error() = default;
    explicit Error(std::string message, Kind kind = Kind::General)
        : message_(std::move(message)), kind_(kind) {}

    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    // is reports whether this error is the given sentinel — the port of
    // errors.Is(err, ErrAbort).
    [[nodiscard]] bool is(Kind kind) const noexcept { return kind_ == kind; }

    friend bool operator==(const Error& a, const Error& b) noexcept {
        return a.kind_ == b.kind_ && a.message_ == b.message_;
    }

private:
    std::string message_;
    Kind kind_ = Kind::General;
};

// abort_error is the ErrAbort sentinel every prompt returns when the user aborts.
// It is a function rather than a variable so the module exports no mutable global.
[[nodiscard]] Error abort_error() {
    return Error("aborted", Error::Kind::Abort);
}

// is_abort reports whether an error is the abort sentinel.
[[nodiscard]] bool is_abort(const Error& e) noexcept {
    return e.is(Error::Kind::Abort);
}

}  // namespace utils::error
