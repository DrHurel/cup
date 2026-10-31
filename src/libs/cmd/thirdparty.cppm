module;
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module cmd:thirdparty;

export import error;
export import project;

// Declarations only, defined in Thirdparty.cpp. Mirrors internal/cmd/thirdparty.go:
// `cup register` vendors a third-party dependency via git submodule, CMake
// FetchContent, or an apt package; `cup unregister` unwinds whatever the
// matching registration wrote.
export namespace cmd {

// Dependency identifies one registered third-party dependency: its name and
// how it was fetched (git-submodule, cmake-download, or apt-install).
struct Dependency {
    std::string name;
    std::string method;

    // Hand-written, not `= default`: a defaulted comparison on a struct
    // exported from a module interface crashes GCC (internal compiler error)
    // when an importing translation unit uses it; still the case in GCC 15.2
    // and GCC 16 as of 2026-10-31. The full note, including when to re-check
    // (each time the project's GCC floor is raised), is above DockerImage in
    // project's Config.cppm. NOSONAR: cpp:S6230 wants `= default` here;
    // that's the crash.
    friend bool operator==(const Dependency& lhs, const Dependency& rhs) {  // NOSONAR
        return lhs.name == rhs.name && lhs.method == rhs.method;
    }
};

// discover_dependencies scans the project's third-party file for
// registrations previously made by `cup register`.
[[nodiscard]] std::vector<Dependency> discover_dependencies(const project::Project& proj);

[[nodiscard]] std::expected<void, error::Error> register_submodule(const project::Project& proj);
[[nodiscard]] std::expected<void, error::Error> register_download(const project::Project& proj);
[[nodiscard]] std::expected<void, error::Error> register_apt(const project::Project& proj);

[[nodiscard]] std::expected<void, error::Error> run_register(std::span<const std::string> args);

// resolve_dependency picks the dependency an unregister acts on: the one
// named in args, or an interactive pick among deps when none is given.
[[nodiscard]] std::expected<Dependency, error::Error> resolve_dependency(
    std::span<const Dependency> deps, std::span<const std::string> args);

[[nodiscard]] std::expected<void, error::Error> remove_submodule(const project::Project& proj,
                                                                  std::string_view name);
[[nodiscard]] std::expected<void, error::Error> remove_download(const project::Project& proj,
                                                                 std::string_view name);
[[nodiscard]] std::expected<void, error::Error> remove_apt(const project::Project& proj,
                                                            std::string_view name);

[[nodiscard]] std::expected<void, error::Error> run_unregister(std::span<const std::string> args);

}
