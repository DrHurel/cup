module;
#include <array>
#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module cup.cmd:add;

export import cup.error;
export import cup.project;
export import cup.ui;

// Declarations only, defined in Add.cpp: the definitions import cup.scaffold
// and cup.tmpl and use <filesystem> throughout, none of which this interface
// partition needs to expose. Covers both add.go (the modules-family
// scaffolding) and add_headers.go (its C++11/14/17 headers-family
// counterpart) — in Go these live in separate files but call into each other
// freely within the same package, so here they share one partition too.
export namespace cup::cmd {

// kCategories are the things `cup add` can scaffold. Exported so
// cup.cmd:completion's shell completions stay in sync, the way Go's
// completion.go reuses add.go's categories var directly.
inline constexpr std::array<std::string_view, 4> kCategories{"app", "lib", "test", "third-party"};

// run_add is the `cup add` entrypoint. With a category argument it scaffolds
// that one target; without one it prompts, then offers to add another.
[[nodiscard]] std::expected<void, error::Error> run_add(std::span<const std::string> args);

// template_family returns the template family (modules / headers) for the
// project's chosen C++ standard. Named to avoid colliding with
// cup::scaffold::family, which it wraps.
[[nodiscard]] std::string_view template_family(const project::Project& proj);

// std_vars builds the variable map passed to scaffold::render: the
// per-standard values (std_lib, std_number, hello, ...) merged with the
// given extra key/value pairs.
[[nodiscard]] std::map<std::string, std::string, std::less<>> std_vars(
    const project::Project& proj,
    std::initializer_list<std::pair<std::string_view, std::string_view>> extra = {});

// pick_or_new offers existing options plus a "[new...]" entry; picking it
// (or having no options at all) prompts for a fresh name.
[[nodiscard]] std::expected<std::string, error::Error> pick_or_new(std::string_view question,
                                                                    std::span<const std::string> options,
                                                                    std::string_view new_prompt,
                                                                    const ui::Validator& validate);

[[nodiscard]] std::expected<void, error::Error> add_app(const project::Project& proj);

[[nodiscard]] std::expected<void, error::Error> add_lib(const project::Project& proj);

// create_lib_at scaffolds a new lib target and registers it with its parent,
// dispatching to the headers family when the project targets C++11/14/17.
[[nodiscard]] std::expected<void, error::Error> create_lib_at(const project::Project& proj,
                                                               std::string_view name,
                                                               const std::filesystem::path& target_dir,
                                                               const std::filesystem::path& parent_cmake);

// primary_preamble returns the global module fragment a lib's primary
// interface unit carries before its module declaration — a GCC 14 BMI-merge
// workaround (see the source for the failure it avoids). Exposed for
// testing: a project's std_module setting flips this and every add_lib/
// add_file_to_lib run depends on getting it right.
[[nodiscard]] std::string primary_preamble(const project::Config& cfg);

[[nodiscard]] std::expected<void, error::Error> add_file_to_lib(const project::Project& proj,
                                                                 const std::filesystem::path& lib_dir);

[[nodiscard]] std::expected<void, error::Error> extend_lib(const project::Project& proj,
                                                            const std::filesystem::path& lib_dir);

// choose_test_module prompts for which library the test exercises,
// returning "" when no libraries exist (no prompt at all) or the user picks
// "none".
[[nodiscard]] std::expected<std::string, error::Error> choose_test_module(const project::Project& proj);

// testModuleImport returns the top-of-file line that pulls in the module
// under test, or "" when there is none.
[[nodiscard]] std::string test_module_import(const project::Project& proj, std::string_view module);

[[nodiscard]] std::expected<void, error::Error> add_test(const project::Project& proj);

// choose_kind prompts for a library-component template kind, defaulting to
// "class" when available.
[[nodiscard]] std::expected<std::string, error::Error> choose_kind(const std::filesystem::path& root,
                                                                    std::string_view family);

[[nodiscard]] std::string rel_to(const std::filesystem::path& root, const std::filesystem::path& path);

// create_header_lib_at mirrors create_lib_at for the headers family (C++11/
// 14/17): a lib gathers components into a primary <name>.hpp aggregator
// instead of a module partition set.
[[nodiscard]] std::expected<void, error::Error> create_header_lib_at(
    const project::Project& proj, std::string_view name, const std::filesystem::path& target_dir,
    const std::filesystem::path& parent_cmake);

// add_file_to_header_lib mirrors add_file_to_lib for the headers family.
[[nodiscard]] std::expected<void, error::Error> add_file_to_header_lib(
    const project::Project& proj, const std::filesystem::path& lib_dir);

}
