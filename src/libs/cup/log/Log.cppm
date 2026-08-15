module;
#include <string_view>
export module cup.log:log;

export namespace cup::log {

// init sets up cup's own durable, always-on operational log — separate from
// cup.ui's interactive/user-facing output. Idempotent; safe to call more
// than once (e.g. once per process, or once per test case). Never throws: a
// failure to open the log file just leaves logging inert (debug/info/warn/
// error become no-ops) rather than breaking the CLI. Level and on/off come
// from the CUP_LOG env var (trace|debug|info|warn|error|off, default info)
// — same precedent as NO_COLOR in cup.ui's Color.cppm. Defined in
// Spdlog.cpp: spdlog stays out of this interface partition for the same
// reason curl stays out of cup.platform's :net (see platform/Http.cpp).
void init();

void debug(std::string_view msg);
void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);

}  // namespace cup::log
