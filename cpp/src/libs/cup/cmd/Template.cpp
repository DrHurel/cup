module;
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <utility>
module cup.cmd;

import cup.scaffold;
import cup.tmpl;
import cup.ui;

namespace cup::cmd {

std::expected<void, error::Error> template_list() {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }

    ui::accent("library component kinds (used by `cup add lib`):");
    for (const auto& kind : tmpl::kinds(proj->root, template_family(*proj))) {
        const bool project_origin =
            std::filesystem::is_directory(proj->path(tmpl::kProjectTemplateDir, kind));
        ui::emit_line(std::format("  {:<18} {}", kind, project_origin ? "project" : "built-in"));
    }
    return {};
}

std::expected<void, error::Error> template_new(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto fam = template_family(*proj);

    auto base = ui::select_one("copy which built-in template as a starting point?",
                               tmpl::builtin_kinds(fam), "class");
    if (!base.has_value()) {
        return std::unexpected(std::move(base).error());
    }

    auto name = ui::text("new template kind name?", *base, scaffold::validate_non_empty);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    if (!args.empty()) {
        *name = args[0];
    }

    const std::filesystem::path dst = proj->path(tmpl::kProjectTemplateDir, *name);
    if (std::filesystem::is_directory(dst)) {
        auto ok = ui::confirm(
            std::format("{} already exists. overwrite its files?", rel_to(proj->root, dst)), false);
        if (!ok.has_value()) {
            return std::unexpected(std::move(ok).error());
        }
        if (!*ok) {
            ui::skipped(rel_to(proj->root, dst));
            return {};
        }
    }
    if (auto copied = tmpl::copy_builtin(fam, *base, dst); !copied.has_value()) {
        return std::unexpected(std::move(copied).error());
    }

    ui::wrote((std::filesystem::path(tmpl::kProjectTemplateDir) / *name).string() + "/");
    ui::next(std::format("edit the files in {}, then use the kind in `cup add lib`",
                         rel_to(proj->root, dst)));
    return {};
}

std::expected<void, error::Error> run_template(std::span<const std::string> args) {
    std::string sub = "list";
    std::span<const std::string> rest = args;
    if (!args.empty()) {
        sub = args[0];
        rest = args.subspan(1);
    }
    if (sub == "list") {
        return template_list();
    }
    if (sub == "new") {
        return template_new(rest);
    }
    return std::unexpected(
        error::Error(std::format("unknown template command \"{}\" (want: list, new)", sub)));
}

}
