module;
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
// Partition is :new_project, not :new — GCC 14/15's P1689 dependency scanner
// mis-tokenizes a partition name equal to the `new` keyword and reports it as
// the primary module interface (`cup.cmd` instead of `cup.cmd:new`), which
// collides with cmd.cppm and breaks the Ninja dyndep build.
export module cup.cmd:new_project;

export import cup.error;
export import cup.project;

// Declarations only, defined in New.cpp: the definitions import cup.scaffold,
// cup.platform (git init) and use <filesystem> throughout, none of which
// this interface partition needs to expose.
export namespace cup::cmd {

// run_new bootstraps a new thin cup project: a cup.toml marker, the root
// CMakeLists.txt (or Makefile), the src/{apps,libs} tree, a .gitignore, the
// default build image's Dockerfile, and a git repo. The project carries no
// build tooling of its own — a globally installed cup manages it.
[[nodiscard]] std::expected<void, error::Error> run_new(std::span<const std::string> args);

// resolve_project_name takes the project name from args or prompts for it,
// then validates it as a C++ identifier (unconditionally — even a name taken
// from args is re-checked).
[[nodiscard]] std::expected<std::string, error::Error> resolve_project_name(
    std::span<const std::string> args);

// choose_build_tool asks which build system to scaffold. CMake (the
// default) drives C++11-23 (headers or modules); Make targets the headers
// family (C++11/14/17) with discovery-based Makefiles that stay
// conflict-free on rebase.
[[nodiscard]] std::expected<std::string, error::Error> choose_build_tool();

// standard_choices returns the C++ standards offered for a build tool: all
// of them for CMake, only the headers family (C++11/14/17) for Make, which
// cannot robustly build C++20/23 modules.
[[nodiscard]] std::vector<int> standard_choices(std::string_view tool);

// choose_standard asks which C++ standard the project targets, defaulting to
// the newest offered for the build tool.
[[nodiscard]] std::expected<int, error::Error> choose_standard(std::string_view tool);

// choose_compiler_floors asks which compilers to pin a minimum for, then for
// each chosen one, which version. An unchosen compiler stays 0 (no floor)
// and is left out of cup.toml's [compiler] table and the CMakeLists guard
// entirely. Returns {gcc, clang}.
[[nodiscard]] std::expected<std::pair<int, int>, error::Error> choose_compiler_floors(int std);

// choose_compiler_floor asks for one compiler's minimum version. choices is
// oldest-first; the oldest (most permissive floor) is the default. A lone
// choice is taken without prompting.
[[nodiscard]] std::expected<int, error::Error> choose_compiler_floor(std::string_view name,
                                                                     std::span<const int> choices);

// module_std_setup returns the top-of-file CMake block for the modules
// family: the minimum-version line plus, for a project on the std module,
// the experimental `import std` opt-in. Unused (and empty) for the headers
// family.
[[nodiscard]] std::string module_std_setup(const project::Config& cfg);

// scaffold_project_tree writes the files a fresh project needs beyond its
// cup.toml marker: the root CMakeLists (or Makefile), .gitignore, the empty
// src/{apps,libs} CMakeLists, and the default build image's Dockerfile.
[[nodiscard]] std::expected<void, error::Error> scaffold_project_tree(const project::Project& proj,
                                                                      int std, int gcc, int clang);

[[nodiscard]] std::expected<void, error::Error> scaffold_make_tree(const project::Project& proj,
                                                                   int std);

}
