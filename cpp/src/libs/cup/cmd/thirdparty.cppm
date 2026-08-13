module;
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module cup.cmd:thirdparty;

export import cup.error;
export import cup.project;

// Declarations only, defined in Thirdparty.cpp. Mirrors internal/cmd/thirdparty.go:
// `cup register` vendors a third-party dependency via git submodule, CMake
// FetchContent, or an apt package; `cup unregister` unwinds whatever the
// matching registration wrote.
export namespace cup::cmd {

// Dependency identifies one registered third-party dependency: its name and
// how it was fetched (git-submodule, cmake-download, or apt-install).
struct Dependency {
    std::string name;
    std::string method;

    friend bool operator==(const Dependency& lhs, const Dependency& rhs) {
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
