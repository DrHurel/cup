module;
#include <expected>
#include <string_view>
export module cup.log:log;

export import cup.error;

export namespace cup::log {

// init sets up cup's three independently leveled log streams (user::,
// internal::, external:: below) — durable, always-on operational logs,
// separate from cup.ui's interactive/user-facing output. Idempotent; safe
// to call more than once (e.g. once per process, or once per test case).
// Never throws: a failure to open the log file leaves all three streams
// inert (their debug/info/warn/error become no-ops) rather than breaking
// the CLI — but it is reported back rather than swallowed, so a caller can
// tell the user and point at CUP_LOG=off as the way to silence it. Defined
// in Spdlog.cpp: spdlog stays out of this interface partition for the same
// reason curl stays out of cup.platform's :net (see platform/Http.cpp).
[[nodiscard]] std::expected<void, error::Error> init();

// user: cup detected something wrong with what it was given — bad
// arguments, an invalid project, a declined confirmation — not a bug in cup
// itself. Independently leveled via CUP_LOG_USER (falls back to the shared
// CUP_LOG, default info).
namespace user {
void debug(std::string_view msg);
void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);
}  // namespace user

// internal: an unexpected failure inside cup itself — a filesystem error, an
// invariant that didn't hold. Named "internal" rather than "system" to
// avoid colliding with libc's ::system(). Independently leveled via
// CUP_LOG_INTERNAL (falls back to the shared CUP_LOG, default info).
namespace internal {
void debug(std::string_view msg);
void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);
}  // namespace internal

// external: a call cup made outside its own process — a git/cmake/ctest
// subprocess, an HTTP request to GitHub or a compiler-release feed.
// Independently leveled via CUP_LOG_EXTERNAL (falls back to the shared
// CUP_LOG, default info).
namespace external {
void debug(std::string_view msg);
void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);
}  // namespace external

}  // namespace cup::log
