module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
export module cup.cmd:build;

export import cup.error;
export import cup.project;

// Declarations only, defined in Build.cpp: the definitions import cup.ui,
// cup.scaffold and cup.platform and use <filesystem> throughout, none of
// which this interface partition needs to expose.
export namespace cup::cmd {

// parse_mode peels an optional leading build-mode argument off args
// (Debug/Release/Coverage), defaulting to Debug. The remaining args are
// returned for the caller (e.g. run's app name / "--" separator).
[[nodiscard]] std::pair<std::string, std::span<const std::string>> parse_mode(
    std::span<const std::string> args);

[[nodiscard]] std::filesystem::path build_dir(const project::Project& proj, std::string_view mode);

// configure generates the CMake build system for mode under build/<mode>.
// Make projects need no configure step (the Makefile discovers components at
// build time), so it is a no-op there.
[[nodiscard]] std::expected<void, error::Error> configure(const project::Project& proj,
                                                           std::string_view mode);

// build configures (if needed) then compiles mode.
[[nodiscard]] std::expected<void, error::Error> build(const project::Project& proj,
                                                       std::string_view mode);

[[nodiscard]] std::expected<void, error::Error> run_configure(std::span<const std::string> args);
[[nodiscard]] std::expected<void, error::Error> run_build(std::span<const std::string> args);
[[nodiscard]] std::expected<void, error::Error> run_test(std::span<const std::string> args);

// run_run builds then runs an app binary: `cup run [MODE] [app] [-- args...]`.
[[nodiscard]] std::expected<void, error::Error> run_run(std::span<const std::string> args);

// resolve_app picks the app to run and separates its program arguments. A
// leading non-"--" token names the app; otherwise the sole app is used, or
// the user is asked to choose. A "--" separates cup's args from the
// program's. Exposed for testing, the same way cup.scaffold's
// read_file_lines is exposed beyond its one internal caller.
[[nodiscard]] std::expected<std::pair<std::string, std::vector<std::string>>, error::Error>
resolve_app(const project::Project& proj, std::span<const std::string> rest);

// run_clean removes the entire build/ tree.
[[nodiscard]] std::expected<void, error::Error> run_clean(std::span<const std::string> args);

[[nodiscard]] std::expected<void, error::Error> run_rebuild(std::span<const std::string> args);
[[nodiscard]] std::expected<void, error::Error> run_retest(std::span<const std::string> args);

// run_shell shells out via cup.platform::run_command, logging a "run ..."
// status line first. The one call site the rest of cup.cmd (new/add/docker's
// git init and future docker build/push) goes through, mirroring Go's
// runCommand var doing both in one step.
[[nodiscard]] std::expected<void, error::Error> run_shell(const std::filesystem::path& dir,
                                                          std::string_view name,
                                                          std::span<const std::string> args);

}
