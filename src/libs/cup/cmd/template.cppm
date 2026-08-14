module;
#include <expected>
#include <span>
#include <string>
export module cup.cmd:template_cmd;

// Partition is :template_cmd, not :template — GCC's P1689 dependency
// scanner mis-tokenizes a partition name equal to a C++ keyword (`template`
// is one) the same way it did for :new; see new.cppm's note and
// docs/migration-cpp23.md.
export import cup.error;

// Declarations only, defined in Template.cpp: the definitions import
// cup.project, cup.scaffold, cup.tmpl and cup.ui and use <filesystem>
// throughout, none of which this interface partition needs to expose.
export namespace cup::cmd {

// run_template handles `cup template <list|new>` — inspecting and adding
// project-local scaffolding templates under .cup/templates/.
[[nodiscard]] std::expected<void, error::Error> run_template(std::span<const std::string> args);

[[nodiscard]] std::expected<void, error::Error> template_list();

// template_new copies a built-in template into .cup/templates/<name> so the
// project can edit it or use it as the base for a new kind.
[[nodiscard]] std::expected<void, error::Error> template_new(std::span<const std::string> args);

}
