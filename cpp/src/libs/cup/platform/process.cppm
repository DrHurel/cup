module;
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
export module cup.platform:process;

export import cup.error;

export namespace cup::platform {

// run_command_impl is declared here, defined in Process.cpp: fork/exec/wait
// stay out of this interface partition for the same reason as :net's
// http_get / Http.cpp split.
[[nodiscard]] std::expected<void, error::Error> run_command_impl(
    const std::filesystem::path& dir, std::string_view name, std::span<const std::string> args);

using RunCommandFunc = std::function<std::expected<void, error::Error>(
    const std::filesystem::path&, std::string_view, std::span<const std::string>)>;

// run_command_func is the seam run_command calls through; overridable in
// tests so callers of run_command (cup.cmd's build/run commands) can be
// tested without shelling out to a real cmake/ctest/make.
[[nodiscard]] RunCommandFunc& run_command_func() {
    static RunCommandFunc f = &run_command_impl;
    return f;
}

// run_command runs an external program from dir with inherited stdio (so an
// interactive sub-prompt — a sudo password, a git credential helper — still
// reaches the terminal), returning an error if it fails to start or exits
// non-zero.
[[nodiscard]] std::expected<void, error::Error> run_command(const std::filesystem::path& dir,
                                                            std::string_view name,
                                                            std::span<const std::string> args) {
    return run_command_func()(dir, name, args);
}

}
