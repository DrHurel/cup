module;
#include <string>
#include <utility>
export module cup.error:error;

export namespace cup::error {

class Error {
public:
    enum class Kind {
        General,
        Abort,
    };

    Error() = default;
    explicit Error(std::string message, Kind kind = Kind::General)
        : message_(std::move(message)), kind_(kind) {}

    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    [[nodiscard]] bool is(Kind kind) const noexcept { return kind_ == kind; }

    friend bool operator==(const Error& a, const Error& b) noexcept {
        return a.kind_ == b.kind_ && a.message_ == b.message_;
    }

private:
    std::string message_;
    Kind kind_ = Kind::General;
};

[[nodiscard]] Error abort_error() {
    return Error("aborted", Error::Kind::Abort);
}

[[nodiscard]] bool is_abort(const Error& e) noexcept {
    return e.is(Error::Kind::Abort);
}

}
