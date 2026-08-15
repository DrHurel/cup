module;
#include <algorithm>
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
#include <cup/version.h>
module cup.cmd;

import cup.scaffold;
import cup.ui;
import cup.platform;

namespace cup::cmd {
namespace {

// version stamped into a fresh project's cup.toml — cup's own cup_version,
// baked in at build time from this project's cup.toml (see the top-level
// CMakeLists.txt's "Version info" section).
inline constexpr std::string_view kCupVersion = CUP_VERSION;

inline constexpr std::string_view kBoth = "gcc and clang";
inline constexpr std::string_view kGccOnly = "gcc only";
inline constexpr std::string_view kClangOnly = "clang only";

std::string lowered(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

std::expected<std::string, error::Error> resolve_project_name(std::span<const std::string> args) {
    std::string name;
    if (!args.empty()) {
        name = args[0];
    } else {
        auto answered = ui::text("project name?", "", scaffold::validate_ident);
        if (!answered.has_value()) {
            return std::unexpected(std::move(answered).error());
        }
        name = *std::move(answered);
    }
    if (auto valid = scaffold::validate_ident(name); !valid.has_value()) {
        return std::unexpected(
            error::Error(std::format("project name \"{}\": {}", name, valid.error().message())));
    }
    return name;
}

std::expected<std::string, error::Error> choose_build_tool() {
    const std::vector options{std::string(project::kToolCMake), std::string(project::kToolMake)};
    return ui::select_one("build system?", options, project::kToolCMake);
}

std::vector<int> standard_choices(std::string_view tool) {
    if (tool != project::kToolMake) {
        return std::vector<int>(scaffold::kStandards.begin(), scaffold::kStandards.end());
    }
    std::vector<int> out;
    for (const int s : scaffold::kStandards) {
        if (!scaffold::uses_modules(s)) {
            out.push_back(s);
        }
    }
    return out;
}

std::expected<int, error::Error> choose_standard(std::string_view tool) {
    const auto stds = standard_choices(tool);
    std::vector<std::string> labels;
    labels.reserve(stds.size());
    for (const int s : stds) {
        labels.push_back(scaffold::std_label(s));
    }
    auto choice = ui::select_one("c++ standard?", labels, labels.front());
    if (!choice.has_value()) {
        return std::unexpected(std::move(choice).error());
    }
    return scaffold::parse_std(*choice);
}

std::expected<int, error::Error> choose_compiler_floor(std::string_view name,
                                                        std::span<const int> choices) {
    if (choices.size() == 1) {
        return choices[0];
    }
    std::vector<std::string> labels;
    labels.reserve(choices.size());
    for (const int v : choices) {
        labels.push_back(std::to_string(v));
    }
    auto choice = ui::select_one(std::format("minimum {} version?", name), labels, labels.front());
    if (!choice.has_value()) {
        return std::unexpected(std::move(choice).error());
    }
    return std::stoi(*choice);
}

std::expected<std::pair<int, int>, error::Error> choose_compiler_floors(int std) {
    const std::vector options{std::string(kBoth), std::string(kGccOnly), std::string(kClangOnly)};
    auto which = ui::select_one("pin a minimum version for which compilers?", options, kBoth);
    if (!which.has_value()) {
        return std::unexpected(std::move(which).error());
    }

    const auto [newest_gcc, newest_clang] = scaffold::newest_compilers();
    const auto [gcc_choices, clang_choices] = scaffold::compiler_choices(std, newest_gcc, newest_clang);

    int gcc = 0;
    int clang = 0;
    if (*which != kClangOnly) {
        auto g = choose_compiler_floor("gcc", gcc_choices);
        if (!g.has_value()) {
            return std::unexpected(std::move(g).error());
        }
        gcc = *g;
    }
    if (*which != kGccOnly) {
        auto c = choose_compiler_floor("clang", clang_choices);
        if (!c.has_value()) {
            return std::unexpected(std::move(c).error());
        }
        clang = *c;
    }
    return std::pair{gcc, clang};
}

std::string module_std_setup(const project::Config& cfg) {
    if (cfg.uses_std_module()) {
        return "# `import std;` requires CMake >= 3.30 (CMAKE_CXX_MODULE_STD support for GCC)\n"
               "# and a compiler that ships the std-module manifest (GCC 15+).\n"
               "cmake_minimum_required(VERSION 3.30)\n"
               "\n"
               "# Opt in to CMake's still-experimental `import std` support. The gate value is a\n"
               "# CMake-version-specific UUID that must match exactly; this one is for CMake 4.4.\n"
               "# Bump it if you move to another CMake.\n"
               "set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD \"f35a9ac6-8463-4d38-8eec-5d6008153e7d\")\n"
               "\n"
               "# Build and provide the `std` module to every C++23 target so sources can\n"
               "# `import std;`. This MUST be set before CXX is enabled by project(): compiler\n"
               "# support for the std module is detected there and skipped if it is off.\n"
               "set(CMAKE_CXX_MODULE_STD ON)\n";
    }
    return "# Named modules need CMake >= 3.28.\n"
           "cmake_minimum_required(VERSION 3.28)\n";
}

std::expected<void, error::Error> scaffold_project_tree(const project::Project& proj, int std,
                                                         int gcc, int clang) {
    if (proj.uses_make()) {
        return scaffold_make_tree(proj, std);
    }
    const auto& root = proj.root;
    const auto fam = scaffold::family(std);

    const std::map<std::string, std::string, std::less<>> root_vars{
        {"name", proj.config.name},
        {"standard", std::to_string(std)},
        {"module_std_setup", module_std_setup(proj.config)},
        {"compiler_guard", scaffold::compiler_guard(gcc, clang)},
    };
    auto root_cmake = scaffold::render(root, fam, "project", "CMakeLists.txt.tmpl", root_vars);
    if (!root_cmake.has_value()) {
        return std::unexpected(std::move(root_cmake).error());
    }
    if (auto wrote = scaffold::write_file(root, root / "CMakeLists.txt", *root_cmake);
        !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    auto gitignore = scaffold::render(root, fam, "project", "gitignore.tmpl", {});
    if (!gitignore.has_value()) {
        return std::unexpected(std::move(gitignore).error());
    }
    if (auto wrote = scaffold::write_file(root, root / ".gitignore", *gitignore);
        !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    // src/apps and src/libs are always add_subdirectory'd by the root
    // CMakeLists, so each needs a (initially empty) CMakeLists.txt from the start.
    for (const std::string_view sub : {"apps", "libs"}) {
        if (auto ensured = scaffold::ensure_file(root, root / "src" / sub / "CMakeLists.txt", "");
            !ensured.has_value()) {
            return std::unexpected(std::move(ensured).error());
        }
    }

    return sync_default_build_image(proj);
}

std::expected<void, error::Error> scaffold_make_tree(const project::Project& proj, int std) {
    const auto& root = proj.root;
    const std::map<std::string, std::string, std::less<>> vars{
        {"name", proj.config.name},
        {"std_number", std::to_string(std)},
    };
    auto makefile = scaffold::render(root, project::kToolMake, "project", "Makefile.tmpl", vars);
    if (!makefile.has_value()) {
        return std::unexpected(std::move(makefile).error());
    }
    if (auto wrote = scaffold::write_file(root, root / "Makefile", *makefile); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    auto gitignore = scaffold::render(root, project::kToolMake, "project", "gitignore.tmpl", {});
    if (!gitignore.has_value()) {
        return std::unexpected(std::move(gitignore).error());
    }
    if (auto wrote = scaffold::write_file(root, root / ".gitignore", *gitignore);
        !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }

    // src/apps and src/libs need to exist so the Makefile's discovery has
    // somewhere to look; unlike CMake they carry no CMakeLists.txt.
    for (const std::string_view sub : {"apps", "libs"}) {
        std::error_code ec;
        std::filesystem::create_directories(proj.src() / sub, ec);
        if (ec) {
            return std::unexpected(error::Error(
                std::format("creating {}: {}", (proj.src() / sub).string(), ec.message())));
        }
    }

    return sync_default_build_image(proj);
}

std::expected<void, error::Error> run_new(std::span<const std::string> args) {
    auto name = resolve_project_name(args);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }

    const std::filesystem::path root = std::filesystem::absolute(*name);
    if (std::filesystem::exists(root)) {
        return std::unexpected(error::Error(std::format("{} already exists", *name)));
    }

    auto tool = choose_build_tool();
    if (!tool.has_value()) {
        return std::unexpected(std::move(tool).error());
    }

    auto std_choice = choose_standard(*tool);
    if (!std_choice.has_value()) {
        return std::unexpected(std::move(std_choice).error());
    }

    auto floors = choose_compiler_floors(*std_choice);
    if (!floors.has_value()) {
        return std::unexpected(std::move(floors).error());
    }
    const auto [gcc, clang] = *floors;

    auto base = choose_base_image();
    if (!base.has_value()) {
        return std::unexpected(std::move(base).error());
    }

    // The default build image shares the project's (lowercased) name; cup
    // keeps its docker/<name>/Dockerfile in sync with the project's apt
    // dependencies.
    project::Project proj{
        root,
        project::Config{
            .name = *name,
            .cup_version = std::string(kCupVersion),
            .cpp_standard = *std_choice,
            .build_tool = *tool,
            .compiler = project::make_compiler_config(gcc, clang),
            .docker = project::DockerConfig{
                .images = {project::DockerImage{
                    .name = lowered(*name), .base = *base, .is_default = true}}},
        },
    };

    // The marker must exist before the scaffold helpers, which resolve paths
    // and template overrides relative to the project root, can log against it.
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        return std::unexpected(error::Error(std::format("creating {}: {}", root.string(), ec.message())));
    }
    if (auto wrote = project::write_config(root, proj.config); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    ui::wrote(project::kMarker);

    if (auto tree = scaffold_project_tree(proj, *std_choice, gcc, clang); !tree.has_value()) {
        return std::unexpected(std::move(tree).error());
    }

    if (auto git = run_shell(root, "git", std::vector<std::string>{"init", "-q"}); !git.has_value()) {
        ui::skipped("git init failed; initialise the repository yourself");
    }

    ui::success("done.");
    ui::next(std::format("cd {}", *name));
    ui::next("cup add app     # scaffold your first executable");
    ui::next("cup build       # compile (Debug)");
    ui::next("cup docker build   # build the toolchain image");
    return {};
}

}
