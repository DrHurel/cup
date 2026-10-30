module;
#include <functional>
#include <utility>
export module core:raw_mode;

export namespace core {

// RawMode is the guard enter_raw_mode hands back: while it owns the terminal,
// the terminal stays raw; destroying (or restore()-ing) it puts the saved
// settings back, exactly once. What "the saved settings" are — a termios on
// POSIX, a console mode on Windows — is the platform backend's business: it
// hands over a callback that restores them, so this type (and everything in
// core's portable core) stays free of OS headers.
class RawMode {
public:
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

    RawMode(RawMode&& other) noexcept
        : restore_(std::move(other.restore_)), active_(std::exchange(other.active_, false)) {}

    RawMode& operator=(RawMode&& other) noexcept {
        if (this != &other) {
            restore();
            restore_ = std::move(other.restore_);
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }

    ~RawMode() { restore(); }

    void restore() noexcept {
        if (active_) {
            active_ = false;
            restore_();
        }
    }

    [[nodiscard]] bool owns() const noexcept { return active_; }

    // make_active is how a backend builds the guard once the terminal is
    // raw: restore puts the settings it saved beforehand back.
    [[nodiscard]] static RawMode make_active(std::function<void()> restore) {
        return RawMode(std::move(restore));
    }

private:
    explicit RawMode(std::function<void()> restore)
        : restore_(std::move(restore)), active_(true) {}

    std::function<void()> restore_;
    bool active_ = false;
};

}
