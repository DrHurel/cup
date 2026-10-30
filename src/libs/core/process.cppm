module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
export module core:process;

export import error;
import :services;

export namespace core {

// run_command_func is the seam run_command calls through: the injected
// backend's entry in the Services table, returned by reference so tests can
// override it and callers of run_command (cmd's build/run commands) can be
// tested without shelling out to a real cmake/ctest/make.
[[nodiscard]] RunCommandFunc& run_command_func() { return services().run_command; }

// run_command runs an external program from dir with inherited stdio (so an
// interactive sub-prompt — a sudo password, a git credential helper — still
// reaches the terminal), returning an error if it fails to start or exits
// non-zero.
[[nodiscard]] std::expected<void, error::Error> run_command(const std::filesystem::path& dir,
                                                            std::string_view name,
                                                            std::span<const std::string> args) {
    return run_command_func()(dir, name, args);
}

// capture_command_func is capture_command's seam, overridable in tests the
// same way run_command_func is.
[[nodiscard]] CaptureCommandFunc& capture_command_func() { return services().capture_command; }

// capture_command runs an external program from dir and returns its stdout,
// for callers that need the output (e.g. parsing `cmake --version`) rather
// than a pass-through terminal. Unlike run_command, stdin/stderr are not
// forwarded, so it is not a fit for anything interactive.
[[nodiscard]] std::expected<std::string, error::Error> capture_command(
    const std::filesystem::path& dir, std::string_view name, std::span<const std::string> args) {
    return capture_command_func()(dir, name, args);
}

}
