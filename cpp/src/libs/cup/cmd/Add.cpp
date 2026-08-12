module;
#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
module cup.cmd;

import cup.scaffold;
import cup.tmpl;
import cup.ui;

namespace cup::cmd {
namespace {

constexpr std::string_view kCMakeLists = "CMakeLists.txt";
constexpr std::string_view kNewSentinel = "[new…]";
constexpr std::string_view kNoneSentinel = "[none]";
constexpr std::array<std::string_view, 4> kCategories{"app", "lib", "test", "third-party"};

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// --- headers-family (C++11/14/17) private helpers, mirroring add_headers.go ---

std::string_view header_ext(bool compiled) { return compiled ? ".h" : ".hpp"; }
std::string_view source_tmpl(bool compiled) { return compiled ? "source.h.tmpl" : "source.hpp.tmpl"; }

// headerComponent is one component of a header lib — the identifiers needed
// to render its sources and wire them into the lib. Threaded through the
// helpers below in place of a long, easily-transposed parameter list.
struct HeaderComponent {
    std::string kind;
    std::filesystem::path lib_dir;
    std::string lib;
    std::string filename;
    std::string symbol;
    std::string namespace_;
    bool compiled = false;

    [[nodiscard]] std::string header() const { return filename + std::string(header_ext(compiled)); }
};

// writeComponentHeader renders and writes a component's header: the
// declaration header (<filename>.h) for a compiled kind, the whole header
// (<filename>.hpp) for a header-only one. Reports whether the file was
// written — a declined overwrite reports false with a nil error.
std::expected<bool, error::Error> write_component_header(const project::Project& proj,
                                                          const HeaderComponent& c) {
    auto src = scaffold::render(proj.root, "headers", c.kind, std::string(source_tmpl(c.compiled)),
                                std_vars(proj, {{"symbol", c.symbol}, {"namespace", c.namespace_}}));
    if (!src.has_value()) {
        return std::unexpected(std::move(src).error());
    }
    return scaffold::write_file(proj.root, c.lib_dir / c.header(), *src);
}

// writeCompiledSource renders and writes a compiled component's
// <filename>.cpp definition. Build-system-agnostic — shared by the CMake
// wiring and the Make path.
std::expected<void, error::Error> write_compiled_source(const project::Project& proj,
                                                         const HeaderComponent& c) {
    auto cpp = scaffold::render(
        proj.root, "headers", c.kind, "source.cpp.tmpl",
        std_vars(proj, {{"symbol", c.symbol}, {"namespace", c.namespace_}, {"header", c.filename + ".h"}}));
    if (!cpp.has_value()) {
        return std::unexpected(std::move(cpp).error());
    }
    auto wrote = scaffold::write_file(proj.root, c.lib_dir / (c.filename + ".cpp"), *cpp);
    if (!wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    return {};
}

// renderComponent writes a lib's first component: for a compiled kind the
// <symbol>.h / <symbol>.cpp pair, for a header-only kind the single
// <symbol>.hpp.
std::expected<void, error::Error> render_component(const project::Project& proj,
                                                    const HeaderComponent& c) {
    auto wrote = write_component_header(proj, c);
    if (!wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (!c.compiled) {
        return {};
    }
    return write_compiled_source(proj, c);
}

// writeHeaderAggregator creates a lib's primary header as a thin aggregator
// that #includes one component header. A declined overwrite leaves the
// existing primary untouched. Mirrors write_primary_aggregator.
std::expected<void, error::Error> write_header_aggregator(const project::Project& proj,
                                                           const std::filesystem::path& primary,
                                                           std::string_view include) {
    auto wrote = scaffold::write_file(proj.root, primary, "#pragma once\n");
    if (!wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (!*wrote) {
        return {};
    }
    return scaffold::ensure_line(proj.root, primary, std::format("#include \"{}\"", include));
}

// addCompiledDefinition wires a compiled component's definition into an
// existing lib: it promotes the lib to STATIC (a no-op if already so),
// writes the <filename>.cpp, and lists it among the lib's PRIVATE sources.
std::expected<void, error::Error> add_compiled_definition(const project::Project& proj,
                                                           const HeaderComponent& c) {
    const std::filesystem::path cmake = c.lib_dir / kCMakeLists;
    if (auto s = scaffold::ensure_header_lib_static(proj.root, cmake, c.lib); !s.has_value()) {
        return std::unexpected(std::move(s).error());
    }
    if (auto w = write_compiled_source(proj, c); !w.has_value()) {
        return std::unexpected(std::move(w).error());
    }
    return scaffold::ensure_line(proj.root, cmake,
                                 std::format("target_sources({} PRIVATE {}.cpp)", c.lib, c.filename));
}

// wireHeaderComponent registers a component whose header has just been
// written with the lib's build system. Mirrors the Go original's Make/CMake
// branching.
std::expected<void, error::Error> wire_header_component(const project::Project& proj,
                                                         const HeaderComponent& c) {
    if (proj.uses_make()) {
        if (!c.compiled) {
            return {};
        }
        return write_compiled_source(proj, c);
    }
    if (c.compiled) {
        if (auto d = add_compiled_definition(proj, c); !d.has_value()) {
            return std::unexpected(std::move(d).error());
        }
    }
    return scaffold::add_header_source(proj.root, c.lib_dir / kCMakeLists, c.header());
}

// --- modules-family private helpers, mirroring add.go ---

// writePrimaryAggregator creates a lib's primary interface unit as a thin
// aggregator over one partition. A declined overwrite leaves the existing
// primary untouched.
std::expected<void, error::Error> write_primary_aggregator(const project::Project& proj,
                                                            const std::filesystem::path& primary,
                                                            std::string_view module,
                                                            std::string_view partition) {
    const std::string content = primary_preamble(proj.config) + std::format("export module {};\n", module);
    auto wrote = scaffold::write_file(proj.root, primary, content);
    if (!wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (!*wrote) {
        return {};
    }
    return scaffold::add_partition_import(proj.root, primary, partition);
}

std::expected<void, error::Error> dispatch_category(const project::Project& proj,
                                                     std::string_view category) {
    if (category == "app") {
        return add_app(proj);
    }
    if (category == "lib") {
        return add_lib(proj);
    }
    if (category == "test") {
        return add_test(proj);
    }
    if (category == "third-party") {
        return std::unexpected(
            error::Error("`cup register` is not ported yet (Phase 4 group 5 of the migration)"));
    }
    return std::unexpected(error::Error(std::format("unknown category: \"{}\"", category)));
}

}  // namespace

std::string_view template_family(const project::Project& proj) {
    return scaffold::family(proj.config.standard());
}

std::map<std::string, std::string, std::less<>> std_vars(
    const project::Project& proj,
    std::initializer_list<std::pair<std::string_view, std::string_view>> extra) {
    auto vars = scaffold::std_vars(proj.config.standard(), proj.config.uses_std_module());
    for (const auto& [k, v] : extra) {
        vars[std::string(k)] = std::string(v);
    }
    return vars;
}

std::expected<std::string, error::Error> pick_or_new(std::string_view question,
                                                      std::span<const std::string> options,
                                                      std::string_view new_prompt,
                                                      const ui::Validator& validate) {
    if (options.empty()) {
        return ui::text(new_prompt, "", validate);
    }
    std::vector<std::string> extended(options.begin(), options.end());
    extended.emplace_back(kNewSentinel);
    auto choice = ui::select_one(question, extended, options[0]);
    if (!choice.has_value()) {
        return std::unexpected(std::move(choice).error());
    }
    if (*choice == kNewSentinel) {
        return ui::text(new_prompt, "", validate);
    }
    return *std::move(choice);
}

std::expected<void, error::Error> run_add(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }

    if (!args.empty()) {
        return dispatch_category(*proj, args[0]);
    }

    const std::vector<std::string> cats(kCategories.begin(), kCategories.end());
    while (true) {
        auto category = ui::select_one("what do you want to add?", cats, "app");
        if (!category.has_value()) {
            return std::unexpected(std::move(category).error());
        }
        if (auto done = dispatch_category(*proj, *category); !done.has_value()) {
            return std::unexpected(std::move(done).error());
        }
        ui::success("done.");
        auto again = ui::confirm("add another?", true);
        if (!again.has_value() || !*again) {
            return {};
        }
        ui::emit_line("");
    }
}

std::expected<void, error::Error> add_app(const project::Project& proj) {
    auto name = ui::text("app name?", "", scaffold::validate_ident);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    auto filename = ui::text("source filename?", *name + ".cpp");
    if (!filename.has_value()) {
        return std::unexpected(std::move(filename).error());
    }

    const std::filesystem::path app_dir = proj.src() / "apps" / *name;
    const std::string namespace_ = scaffold::path_to_namespace(proj.src().string(), app_dir.string());
    const auto fam = template_family(proj);

    auto src = scaffold::render(proj.root, fam, "app", "source.cpp.tmpl",
                                std_vars(proj, {{"name", *name}, {"namespace", namespace_}}));
    if (!src.has_value()) {
        return std::unexpected(std::move(src).error());
    }
    if (auto wrote = scaffold::write_file(proj.root, app_dir / *filename, *src); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    // Under Make the root Makefile discovers this app by path — no
    // CMakeLists and no parent-file registration, so the add touches only
    // the new directory.
    if (proj.uses_make()) {
        return {};
    }

    auto cml = scaffold::render(proj.root, fam, "app", "CMakeLists.txt.tmpl",
                                std_vars(proj, {{"name", *name}, {"filename", *filename}}));
    if (!cml.has_value()) {
        return std::unexpected(std::move(cml).error());
    }
    if (auto wrote = scaffold::write_file(proj.root, app_dir / kCMakeLists, *cml); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    return scaffold::ensure_line(proj.root, proj.src() / "apps" / kCMakeLists,
                                 std::format("add_subdirectory({})", *name));
}

std::expected<void, error::Error> add_lib(const project::Project& proj) {
    const std::filesystem::path libs_dir = proj.src() / "libs";
    const auto existing = scaffold::list_subdirs(libs_dir);
    auto name = pick_or_new("lib name?", existing, "new lib name?", scaffold::validate_ident);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    const std::filesystem::path lib_dir = libs_dir / *name;
    if (std::filesystem::is_directory(lib_dir)) {
        return extend_lib(proj, lib_dir);
    }
    return create_lib_at(proj, *name, lib_dir, libs_dir / kCMakeLists);
}

std::expected<void, error::Error> create_lib_at(const project::Project& proj, std::string_view name,
                                                const std::filesystem::path& target_dir,
                                                const std::filesystem::path& parent_cmake) {
    if (!proj.uses_modules()) {
        return create_header_lib_at(proj, name, target_dir, parent_cmake);
    }
    auto kind = choose_kind(proj.root, template_family(proj));
    if (!kind.has_value()) {
        return std::unexpected(std::move(kind).error());
    }

    const std::string module = scaffold::path_to_module(proj.src().string(), target_dir.string());
    auto symbol = ui::text("primary symbol name?", scaffold::capitalize(name), scaffold::validate_ident);
    if (!symbol.has_value()) {
        return std::unexpected(std::move(symbol).error());
    }
    const std::string namespace_ = scaffold::path_to_namespace(proj.src().string(), target_dir.string());
    const std::string partition = lower(*symbol);
    const std::filesystem::path primary = target_dir / (std::string(name) + ".cppm");
    const std::filesystem::path cmake = target_dir / kCMakeLists;

    auto src = scaffold::render(proj.root, "modules", *kind, "source.cppm.tmpl",
                                std_vars(proj, {{"module", module + ":" + partition},
                                               {"symbol", *symbol},
                                               {"namespace", namespace_}}));
    if (!src.has_value()) {
        return std::unexpected(std::move(src).error());
    }
    if (auto wrote = scaffold::write_file(proj.root, target_dir / (*symbol + ".cppm"), *src);
        !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    if (auto agg = write_primary_aggregator(proj, primary, module, partition); !agg.has_value()) {
        return std::unexpected(std::move(agg).error());
    }

    auto cml = scaffold::render(proj.root, "modules", *kind, "CMakeLists.txt.tmpl",
                                std_vars(proj, {{"name", name}}));
    if (!cml.has_value()) {
        return std::unexpected(std::move(cml).error());
    }
    if (auto wrote = scaffold::write_file(proj.root, cmake, *cml); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (auto added = scaffold::add_module_source(proj.root, cmake, *symbol + ".cppm"); !added.has_value()) {
        return std::unexpected(std::move(added).error());
    }
    return scaffold::ensure_line(proj.root, parent_cmake, std::format("add_subdirectory({})", name));
}

std::string primary_preamble(const project::Config& cfg) {
    if (!cfg.uses_std_module()) {
        return "module;\n#include <string>\n";
    }
    return "";
}

std::expected<void, error::Error> add_file_to_lib(const project::Project& proj,
                                                   const std::filesystem::path& lib_dir) {
    if (!proj.uses_modules()) {
        return add_file_to_header_lib(proj, lib_dir);
    }
    auto filename = ui::text("new file name (no extension)?", "", scaffold::validate_ident);
    if (!filename.has_value()) {
        return std::unexpected(std::move(filename).error());
    }
    auto kind = choose_kind(proj.root, template_family(proj));
    if (!kind.has_value()) {
        return std::unexpected(std::move(kind).error());
    }

    const std::string module =
        scaffold::path_to_module(proj.src().string(), lib_dir.string()) + ":" + *filename;
    auto symbol = ui::text("primary symbol name?", scaffold::capitalize(*filename), scaffold::validate_ident);
    if (!symbol.has_value()) {
        return std::unexpected(std::move(symbol).error());
    }
    const std::string namespace_ = scaffold::path_to_namespace(proj.src().string(), lib_dir.string());
    const std::filesystem::path primary = lib_dir / (lib_dir.filename().string() + ".cppm");

    auto src = scaffold::render(
        proj.root, "modules", *kind, "source.cppm.tmpl",
        std_vars(proj, {{"module", module}, {"symbol", *symbol}, {"namespace", namespace_}}));
    if (!src.has_value()) {
        return std::unexpected(std::move(src).error());
    }
    auto wrote = scaffold::write_file(proj.root, lib_dir / (*filename + ".cppm"), *src);
    if (!wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (!*wrote) {
        return {};
    }

    if (auto added = scaffold::add_module_source(proj.root, lib_dir / kCMakeLists, *filename + ".cppm");
        !added.has_value()) {
        return std::unexpected(std::move(added).error());
    }
    return scaffold::add_partition_import(proj.root, primary, *filename);
}

std::expected<void, error::Error> extend_lib(const project::Project& proj,
                                             const std::filesystem::path& lib_dir) {
    const std::vector<std::string> what_options{"file", "subfolder"};
    auto what =
        ui::select_one(std::format("add to '{}' as?", rel_to(proj.root, lib_dir)), what_options, "file");
    if (!what.has_value()) {
        return std::unexpected(std::move(what).error());
    }
    if (*what == "file") {
        return add_file_to_lib(proj, lib_dir);
    }

    std::vector<std::string> existing_subs;
    for (const auto& sub : scaffold::list_subdirs(lib_dir)) {
        if (std::filesystem::is_regular_file(lib_dir / sub / kCMakeLists)) {
            existing_subs.push_back(sub);
        }
    }
    auto sub = pick_or_new("subfolder name?", existing_subs, "new subfolder name?", scaffold::validate_ident);
    if (!sub.has_value()) {
        return std::unexpected(std::move(sub).error());
    }
    const std::filesystem::path sub_dir = lib_dir / *sub;
    if (std::filesystem::is_directory(sub_dir)) {
        return extend_lib(proj, sub_dir);
    }
    return create_lib_at(proj, *sub, sub_dir, lib_dir / kCMakeLists);
}

std::expected<std::string, error::Error> choose_test_module(const project::Project& proj) {
    const auto libs = scaffold::list_subdirs(proj.src() / "libs");
    if (libs.empty()) {
        return std::string{};
    }
    std::vector<std::string> options{std::string(kNoneSentinel)};
    options.insert(options.end(), libs.begin(), libs.end());
    auto picked = ui::select_one("module under test?", options, kNoneSentinel);
    if (!picked.has_value()) {
        return std::unexpected(std::move(picked).error());
    }
    if (*picked == kNoneSentinel) {
        return std::string{};
    }
    return *std::move(picked);
}

std::string test_module_import(const project::Project& proj, std::string_view module) {
    if (module.empty()) {
        return "";
    }
    if (proj.uses_modules()) {
        return std::format("import {};\n", module);
    }
    return std::format("#include \"{}.hpp\"\n", module);
}

std::expected<void, error::Error> add_test(const project::Project& proj) {
    auto name = ui::text("test name?", "", scaffold::validate_ident);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    auto module = choose_test_module(proj);
    if (!module.has_value()) {
        return std::unexpected(std::move(module).error());
    }
    const std::string module_import = test_module_import(proj, *module);

    const std::filesystem::path tests_dir = proj.src() / "tests";
    std::string namespace_ = scaffold::path_to_namespace(proj.src().string(), tests_dir.string());
    if (namespace_.empty()) {
        namespace_ = *name;
    }

    auto src = scaffold::render(
        proj.root, template_family(proj), "test", "source.cpp.tmpl",
        std_vars(proj, {{"name", *name}, {"module_import", module_import}, {"namespace", namespace_}}));
    if (!src.has_value()) {
        return std::unexpected(std::move(src).error());
    }
    if (auto wrote = scaffold::write_file(proj.root, tests_dir / (*name + ".cpp"), *src);
        !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    // Under Make the root Makefile discovers src/tests/*.cpp and links every
    // lib archive (so the module under test is available) — nothing else to
    // wire.
    if (proj.uses_make()) {
        return {};
    }

    const std::filesystem::path tests_cmake = tests_dir / kCMakeLists;
    std::vector<std::string> steps{
        std::format("add_executable({} {}.cpp)", *name, *name),
        std::format("target_compile_features({} PRIVATE cxx_std_{})", *name, proj.config.standard()),
    };
    if (!module->empty()) {
        steps.push_back(std::format("target_link_libraries({} PRIVATE {})", *name, *module));
    }
    steps.push_back(std::format("add_test(NAME {} COMMAND {})", *name, *name));
    for (const auto& line : steps) {
        if (auto ensured = scaffold::ensure_line(proj.root, tests_cmake, line); !ensured.has_value()) {
            return std::unexpected(std::move(ensured).error());
        }
    }
    return scaffold::ensure_line(proj.root, proj.root / kCMakeLists, "add_subdirectory(src/tests)");
}

std::expected<std::string, error::Error> choose_kind(const std::filesystem::path& root,
                                                      std::string_view family) {
    const auto kinds = tmpl::kinds(root, family);
    if (kinds.empty()) {
        return std::unexpected(error::Error("no library templates available"));
    }
    std::string def = kinds.front();
    for (const auto& k : kinds) {
        if (k == "class") {
            def = "class";
            break;
        }
    }
    return ui::select_one("template kind?", kinds, def);
}

std::string rel_to(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    const auto r = std::filesystem::relative(path, root, ec);
    if (ec) {
        return path.string();
    }
    return r.string();
}

std::expected<void, error::Error> create_header_lib_at(const project::Project& proj,
                                                        std::string_view name,
                                                        const std::filesystem::path& target_dir,
                                                        const std::filesystem::path& parent_cmake) {
    auto kind = choose_kind(proj.root, "headers");
    if (!kind.has_value()) {
        return std::unexpected(std::move(kind).error());
    }
    auto symbol = ui::text("primary symbol name?", scaffold::capitalize(name), scaffold::validate_ident);
    if (!symbol.has_value()) {
        return std::unexpected(std::move(symbol).error());
    }

    // A lib's first component is named after its primary symbol.
    HeaderComponent c{
        .kind = *kind,
        .lib_dir = target_dir,
        .lib = std::string(name),
        .filename = *symbol,
        .symbol = *symbol,
        .namespace_ = scaffold::path_to_namespace(proj.src().string(), target_dir.string()),
        .compiled = tmpl::is_compiled(proj.root, "headers", *kind),
    };
    const std::filesystem::path primary = target_dir / (std::string(name) + ".hpp");
    const std::filesystem::path cmake = target_dir / kCMakeLists;

    if (auto r = render_component(proj, c); !r.has_value()) {
        return std::unexpected(std::move(r).error());
    }
    if (auto a = write_header_aggregator(proj, primary, c.header()); !a.has_value()) {
        return std::unexpected(std::move(a).error());
    }
    // Under Make the root Makefile finds this lib's .cpp sources by path and
    // archives them; no CMakeLists and no parent registration are written.
    if (proj.uses_make()) {
        return {};
    }

    // The STATIC CMakeLists seeds its PRIVATE sources with {{symbol}}.cpp, so
    // a compiled kind needs symbol; the INTERFACE template simply ignores it.
    auto cml = scaffold::render(proj.root, "headers", *kind, "CMakeLists.txt.tmpl",
                                std_vars(proj, {{"name", name}, {"symbol", *symbol}}));
    if (!cml.has_value()) {
        return std::unexpected(std::move(cml).error());
    }
    if (auto wrote = scaffold::write_file(proj.root, cmake, *cml); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (auto added = scaffold::add_header_source(proj.root, cmake, c.header()); !added.has_value()) {
        return std::unexpected(std::move(added).error());
    }
    return scaffold::ensure_line(proj.root, parent_cmake, std::format("add_subdirectory({})", name));
}

std::expected<void, error::Error> add_file_to_header_lib(const project::Project& proj,
                                                          const std::filesystem::path& lib_dir) {
    auto filename = ui::text("new file name (no extension)?", "", scaffold::validate_ident);
    if (!filename.has_value()) {
        return std::unexpected(std::move(filename).error());
    }
    auto kind = choose_kind(proj.root, "headers");
    if (!kind.has_value()) {
        return std::unexpected(std::move(kind).error());
    }
    auto symbol = ui::text("primary symbol name?", scaffold::capitalize(*filename), scaffold::validate_ident);
    if (!symbol.has_value()) {
        return std::unexpected(std::move(symbol).error());
    }

    HeaderComponent c{
        .kind = *kind,
        .lib_dir = lib_dir,
        .lib = lib_dir.filename().string(),
        .filename = *filename,
        .symbol = *symbol,
        .namespace_ = scaffold::path_to_namespace(proj.src().string(), lib_dir.string()),
        .compiled = tmpl::is_compiled(proj.root, "headers", *kind),
    };

    auto wrote = write_component_header(proj, c);
    if (!wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    if (!*wrote) {
        return {};
    }
    if (auto wired = wire_header_component(proj, c); !wired.has_value()) {
        return std::unexpected(std::move(wired).error());
    }
    const std::filesystem::path primary = lib_dir / (c.lib + ".hpp");
    return scaffold::ensure_line(proj.root, primary, std::format("#include \"{}\"", c.header()));
}

}
