module;
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
module cup.cmd;

import cup.scaffold;
import cup.ui;

namespace cup::cmd {
namespace {

constexpr std::string_view kMethodSubmodule = "git-submodule";
constexpr std::string_view kMethodDownload = "cmake-download";
constexpr std::string_view kMethodApt = "apt-install";
constexpr std::array<std::string_view, 3> kThirdPartyMethods{kMethodSubmodule, kMethodDownload,
                                                             kMethodApt};

// The vendored-dependency prefix, e.g. third_party/<name>.
constexpr std::string_view kThirdPartyPath = "third_party/";

constexpr std::string_view kThirdPartyHeader =
    "# Third-party dependencies, registered via `cup register`.\n"
    "# git submodules -> add_subdirectory, cmake downloads -> FetchContent,\n"
    "# system packages -> find_package.\n\n"
    "include(FetchContent)\n";

constexpr std::string_view kThirdPartyMakeHeader =
    "# Third-party dependencies, registered via `cup register`.\n"
    "# Included by the root Makefile. Add extra include flags to CUP_TP_INCLUDES and\n"
    "# linker flags to CUP_TP_LIBS. cup appends tracking markers to each entry below\n"
    "# so it can unregister them; edit entries but leave those markers intact.\n\n";

// cup_dep_marker tags a Make CUP_TP_INCLUDES line with the method and name of
// the dependency that added it, so discover_dependencies/remove_* can recover
// it.
constexpr std::string_view kCupDepMarker = "# cup-dep:";

// apt_marker tags a find_package line in third_party/CMakeLists.txt (or a
// third_party.mk line) with the apt package that provides it, so
// cmd::apt_packages can reconstruct the install list the default build image
// needs. Duplicated from Docker.cpp's own copy, matching this port's
// established per-file convention for small string constants.
constexpr std::string_view kAptMarker = "# cup-apt:";

// regex_quote_meta escapes ECMAScript regex metacharacters so a
// user-supplied string (e.g. an apt package name, not restricted to
// identifier syntax) can be embedded into a regex pattern and still only
// match itself literally — the C++ analogue of Go's regexp.QuoteMeta, needed
// wherever a dependency name lands inside a scaffold::remove_matching_line
// pattern.
std::string regex_quote_meta(std::string_view s) {
    static const std::regex kSpecial(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(std::string(s), kSpecial, R"(\$&)");
}

std::filesystem::path third_party_cmake(const project::Project& proj) {
    return proj.path("third_party", "CMakeLists.txt");
}

std::filesystem::path third_party_make(const project::Project& proj) {
    return proj.path("third_party", "third_party.mk");
}

// third_party_file is where registrations are recorded for the project's
// build tool.
std::filesystem::path third_party_file(const project::Project& proj) {
    if (proj.uses_make()) {
        return third_party_make(proj);
    }
    return third_party_cmake(proj);
}

// prepare_third_party ensures the third-party file for the build tool exists
// (and, for CMake, that the root build includes it before src/libs so
// dependencies configure first). The Make root Makefile already `-include`s
// third_party.mk, so no shared-file edit is needed there.
std::expected<void, error::Error> prepare_third_party(const project::Project& proj) {
    if (proj.uses_make()) {
        return scaffold::ensure_file(proj.root, third_party_make(proj), kThirdPartyMakeHeader);
    }
    if (auto ensured = scaffold::ensure_file(proj.root, third_party_cmake(proj), kThirdPartyHeader);
        !ensured.has_value()) {
        return ensured;
    }
    return scaffold::ensure_line_before(proj.root, proj.path("CMakeLists.txt"),
                                        "add_subdirectory(third_party)", "add_subdirectory(src/libs)");
}

// register_make_dep records a vendored dependency in third_party.mk: it adds
// the dependency's directory to the compiler include path and tags the line
// with the method + name so remove_* can unwind it. Header-and-source
// layouts vary, so it points the include at both third_party/<name> and its
// conventional include/ subdir; extra flags go in CUP_TP_LIBS by hand.
std::expected<void, error::Error> register_make_dep(const project::Project& proj,
                                                     std::string_view method, std::string_view name) {
    const std::string line =
        std::format("CUP_TP_INCLUDES += -Ithird_party/{0} -Ithird_party/{0}/include  {1} {2} {0}", name,
                    kCupDepMarker, method);
    return scaffold::ensure_line(proj.root, third_party_make(proj), line);
}

// remove_make_dep_line drops the `# cup-dep: <method> <name>` CUP_TP_INCLUDES
// line that register_make_dep wrote, reporting whether one was found.
std::expected<bool, error::Error> remove_make_dep_line(const project::Project& proj,
                                                        std::string_view method, std::string_view name) {
    const std::string pattern = std::format("{} {} {}\\b", kCupDepMarker, method, name);
    return scaffold::remove_matching_line(proj.root, third_party_make(proj), pattern);
}

std::vector<Dependency> discover_make_dependencies(const project::Project& proj) {
    const auto lines = scaffold::read_file_lines(third_party_make(proj));
    if (!lines.has_value()) {
        return {};
    }
    static const std::regex kCupDepRe(std::format("{}\\s+(\\S+)\\s+(\\S+)", kCupDepMarker));
    std::vector<Dependency> deps;
    for (const auto& raw : *lines) {
        if (std::smatch m; std::regex_search(raw, m, kCupDepRe)) {
            deps.push_back(Dependency{.name = m[2].str(), .method = m[1].str()});
            continue;
        }
        if (const auto idx = raw.find(kAptMarker); idx != std::string::npos) {
            std::istringstream fields(raw.substr(idx + kAptMarker.size()));
            std::string pkg;
            while (fields >> pkg) {
                deps.push_back(Dependency{.name = pkg, .method = std::string(kMethodApt)});
            }
        }
    }
    return deps;
}

}  // namespace

std::vector<Dependency> discover_dependencies(const project::Project& proj) {
    if (proj.uses_make()) {
        return discover_make_dependencies(proj);
    }
    const auto lines = scaffold::read_file_lines(third_party_cmake(proj));
    if (!lines.has_value()) {
        return {};
    }
    static const std::regex kSubmoduleRe(R"(add_subdirectory\(\s*([A-Za-z0-9_./-]+)\s*\))");
    static const std::regex kDownloadRe(R"(FetchContent_MakeAvailable\(\s*([A-Za-z0-9_]+)\s*\))");
    static const std::regex kFindPackageRe(R"(find_package\(\s*([A-Za-z0-9_]+))");

    std::vector<Dependency> deps;
    for (const auto& raw : *lines) {
        std::string line = raw;
        const auto first = line.find_first_not_of(" \t");
        line = first == std::string::npos ? "" : line.substr(first);
        std::smatch m;
        if (line.starts_with("add_subdirectory") && std::regex_search(line, m, kSubmoduleRe)) {
            deps.push_back(Dependency{.name = m[1].str(), .method = std::string(kMethodSubmodule)});
        } else if (line.starts_with("FetchContent_MakeAvailable") &&
                  std::regex_search(line, m, kDownloadRe)) {
            deps.push_back(Dependency{.name = m[1].str(), .method = std::string(kMethodDownload)});
        } else if (line.starts_with("find_package") && std::regex_search(line, m, kFindPackageRe)) {
            deps.push_back(Dependency{.name = m[1].str(), .method = std::string(kMethodApt)});
        }
    }
    return deps;
}

std::expected<void, error::Error> register_submodule(const project::Project& proj) {
    auto name = ui::text("dependency name?", "", scaffold::validate_ident);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    auto url = ui::text("git repository URL?", "", scaffold::validate_non_empty);
    if (!url.has_value()) {
        return std::unexpected(std::move(url).error());
    }
    auto ref = ui::text("branch or tag? (blank for the default branch)", "", {});
    if (!ref.has_value()) {
        return std::unexpected(std::move(ref).error());
    }

    std::vector<std::string> git_args{"submodule", "add"};
    if (!ref->empty()) {
        git_args.emplace_back("--branch");
        git_args.push_back(*ref);
    }
    // A dash-leading URL or ref could otherwise be reinterpreted as a git
    // flag; "--" pins everything after it as positional.
    git_args.emplace_back("--");
    git_args.push_back(*url);
    git_args.push_back(std::string(kThirdPartyPath) + *name);
    if (auto added = run_shell(proj.root, "git", git_args); !added.has_value()) {
        return added;
    }
    if (auto prepared = prepare_third_party(proj); !prepared.has_value()) {
        return prepared;
    }
    if (proj.uses_make()) {
        return register_make_dep(proj, kMethodSubmodule, *name);
    }
    return scaffold::ensure_line(proj.root, third_party_cmake(proj),
                                 std::format("add_subdirectory({})", *name));
}

std::expected<void, error::Error> register_download(const project::Project& proj) {
    auto name = ui::text("dependency name?", "", scaffold::validate_ident);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    auto url = ui::text("git repository URL?", "", scaffold::validate_non_empty);
    if (!url.has_value()) {
        return std::unexpected(std::move(url).error());
    }
    auto tag = ui::text("git tag / ref?", "", scaffold::validate_non_empty);
    if (!tag.has_value()) {
        return std::unexpected(std::move(tag).error());
    }

    // Make has no FetchContent to fetch at configure time, so vendor the
    // sources now with a shallow clone and expose their headers via
    // third_party.mk.
    if (proj.uses_make()) {
        // See register_submodule's matching comment: "--" pins the URL and
        // path as positional regardless of a leading dash.
        std::vector<std::string> clone_args{"clone", "--depth", "1", "--branch", *tag};
        clone_args.emplace_back("--");
        clone_args.push_back(*url);
        clone_args.push_back(std::string(kThirdPartyPath) + *name);
        if (auto cloned = run_shell(proj.root, "git", clone_args); !cloned.has_value()) {
            return cloned;
        }
        if (auto prepared = prepare_third_party(proj); !prepared.has_value()) {
            return prepared;
        }
        return register_make_dep(proj, kMethodDownload, *name);
    }
    const std::string block =
        std::format("FetchContent_Declare(\n  {0}\n  GIT_REPOSITORY {1}\n  GIT_TAG {2}\n)\n"
                    "FetchContent_MakeAvailable({0})\n",
                    *name, *url, *tag);
    if (auto prepared = prepare_third_party(proj); !prepared.has_value()) {
        return prepared;
    }
    return scaffold::append_block(proj.root, third_party_cmake(proj),
                                  std::format("FetchContent_MakeAvailable({})", *name), block);
}

std::expected<void, error::Error> register_apt(const project::Project& proj) {
    if (proj.uses_make()) {
        auto pkg = ui::text("apt package name?", "", scaffold::validate_non_empty);
        if (!pkg.has_value()) {
            return std::unexpected(std::move(pkg).error());
        }
        auto install =
            ui::confirm(std::format("run 'sudo apt-get install -y {}' now?", *pkg), true);
        if (!install.has_value()) {
            return std::unexpected(std::move(install).error());
        }
        if (auto prepared = prepare_third_party(proj); !prepared.has_value()) {
            return prepared;
        }
        if (*install) {
            if (auto installed =
                    run_shell(proj.root, "sudo", std::vector<std::string>{"apt-get", "install", "-y", *pkg});
                !installed.has_value()) {
                return installed;
            }
        }
        if (auto ensured = scaffold::ensure_line(proj.root, third_party_make(proj),
                                                 std::string(kAptMarker) + " " + *pkg);
            !ensured.has_value()) {
            return ensured;
        }
        return sync_default_build_image(proj);
    }

    auto name = ui::text("find_package name?", "", scaffold::validate_ident);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    std::string lowered = *name;
    for (char& c : lowered) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    auto pkg = ui::text("apt package name?", lowered, scaffold::validate_non_empty);
    if (!pkg.has_value()) {
        return std::unexpected(std::move(pkg).error());
    }
    auto install = ui::confirm(std::format("run 'sudo apt-get install -y {}' now?", *pkg), true);
    if (!install.has_value()) {
        return std::unexpected(std::move(install).error());
    }
    if (auto prepared = prepare_third_party(proj); !prepared.has_value()) {
        return prepared;
    }
    if (*install) {
        if (auto installed =
                run_shell(proj.root, "sudo", std::vector<std::string>{"apt-get", "install", "-y", *pkg});
            !installed.has_value()) {
            return installed;
        }
    }
    // Tag the line with the apt package name so the build image can
    // reinstall it (the find_package name and the apt package name often
    // differ, e.g. find_package(Boost) <- apt libboost-dev).
    const std::string line = std::format("find_package({} REQUIRED) {} {}", *name, kAptMarker, *pkg);
    if (auto ensured = scaffold::ensure_line(proj.root, third_party_cmake(proj), line);
        !ensured.has_value()) {
        return ensured;
    }
    return sync_default_build_image(proj);
}

std::expected<void, error::Error> run_register(std::span<const std::string> args) {
    static_cast<void>(args);
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const std::vector<std::string> methods(kThirdPartyMethods.begin(), kThirdPartyMethods.end());
    auto method = ui::select_one("how should the dependency be fetched?", methods,
                                 kMethodSubmodule);
    if (!method.has_value()) {
        return std::unexpected(std::move(method).error());
    }
    if (*method == kMethodSubmodule) {
        return register_submodule(*proj);
    }
    if (*method == kMethodDownload) {
        return register_download(*proj);
    }
    if (*method == kMethodApt) {
        return register_apt(*proj);
    }
    return std::unexpected(error::Error(std::format("unknown method: \"{}\"", *method)));
}

std::expected<Dependency, error::Error> resolve_dependency(std::span<const Dependency> deps,
                                                            std::span<const std::string> args) {
    if (!args.empty()) {
        for (const auto& d : deps) {
            if (d.name == args[0]) {
                return d;
            }
        }
        std::string known;
        for (std::size_t i = 0; i < deps.size(); ++i) {
            if (i != 0) {
                known += ", ";
            }
            known += deps[i].name;
        }
        return std::unexpected(
            error::Error(std::format("no registered dependency named \"{}\". Known: {}", args[0], known)));
    }
    std::vector<std::string> labels;
    labels.reserve(deps.size());
    for (const auto& d : deps) {
        labels.push_back(std::format("{}  ({})", d.name, d.method));
    }
    auto picked = ui::select_one("which dependency should be removed?", labels, labels.front());
    if (!picked.has_value()) {
        return std::unexpected(std::move(picked).error());
    }
    const auto sep = picked->find("  (");
    const std::string picked_name = sep == std::string::npos ? *picked : picked->substr(0, sep);
    for (const auto& d : deps) {
        if (d.name == picked_name) {
            return d;
        }
    }
    return std::unexpected(error::Error("internal error: picked dependency not found"));
}

std::expected<void, error::Error> remove_submodule(const project::Project& proj, std::string_view name) {
    const std::string sub_path = std::string(kThirdPartyPath) + std::string(name);
    if (auto deinit = run_shell(proj.root, "git", std::vector<std::string>{"submodule", "deinit", "-f", sub_path});
        !deinit.has_value()) {
        return deinit;
    }
    if (auto removed = run_shell(proj.root, "git", std::vector<std::string>{"rm", "-f", sub_path});
        !removed.has_value()) {
        return removed;
    }
    scaffold::remove_dir(proj.path(".git", "modules", "third_party", name));
    if (proj.uses_make()) {
        if (auto removed = remove_make_dep_line(proj, kMethodSubmodule, name); !removed.has_value()) {
            return std::unexpected(std::move(removed).error());
        }
        return {};
    }
    if (auto removed = scaffold::remove_line(proj.root, third_party_cmake(proj),
                                             std::format("add_subdirectory({})", name));
        !removed.has_value()) {
        return std::unexpected(std::move(removed).error());
    }
    return {};
}

std::expected<void, error::Error> remove_download(const project::Project& proj, std::string_view name) {
    if (proj.uses_make()) {
        scaffold::remove_dir(proj.path("third_party", name));
        auto removed = remove_make_dep_line(proj, kMethodDownload, name);
        if (!removed.has_value()) {
            return std::unexpected(std::move(removed).error());
        }
        if (!*removed) {
            return std::unexpected(error::Error(
                std::format("no download dependency \"{}\" found in third_party/third_party.mk", name)));
        }
        return {};
    }
    auto removed = scaffold::remove_fetch_content_block(proj.root, third_party_cmake(proj), name);
    if (!removed.has_value()) {
        return std::unexpected(std::move(removed).error());
    }
    if (!*removed) {
        return std::unexpected(
            error::Error(std::format("no FetchContent block for \"{}\" found in third_party/CMakeLists.txt", name)));
    }
    return {};
}

std::expected<void, error::Error> remove_apt(const project::Project& proj, std::string_view name) {
    const std::string quoted_name = regex_quote_meta(name);
    std::string pattern;
    std::string msg;
    if (proj.uses_make()) {
        // The apt marker line is `# cup-apt: <pkg>`; the pkg is the
        // dependency name.
        pattern = std::format("{}.*\\b{}\\b", kAptMarker, quoted_name);
        msg = std::format("no apt dependency \"{}\" found in third_party/third_party.mk", name);
    } else {
        pattern = std::format("find_package\\(\\s*{}\\b", quoted_name);
        msg = std::format("no find_package({} ...) line found in third_party/CMakeLists.txt", name);
    }
    auto removed = scaffold::remove_matching_line(proj.root, third_party_file(proj), pattern);
    if (!removed.has_value()) {
        return std::unexpected(std::move(removed).error());
    }
    if (!*removed) {
        return std::unexpected(error::Error(msg));
    }
    if (auto synced = sync_default_build_image(proj); !synced.has_value()) {
        return synced;
    }
    ui::skipped(
        std::format("the apt package for {} is left installed; remove it with apt if unwanted", name));
    return {};
}

std::expected<void, error::Error> run_unregister(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto deps = discover_dependencies(*proj);
    if (deps.empty()) {
        ui::accent("no third-party dependencies registered — nothing to remove.");
        return {};
    }

    auto dep = resolve_dependency(deps, args);
    if (!dep.has_value()) {
        return std::unexpected(std::move(dep).error());
    }
    auto ok = ui::confirm(std::format("remove {} ({})?", dep->name, dep->method), false);
    if (!ok.has_value()) {
        return std::unexpected(std::move(ok).error());
    }
    if (!*ok) {
        ui::skipped(dep->name);
        return {};
    }
    if (dep->method == kMethodSubmodule) {
        return remove_submodule(*proj, dep->name);
    }
    if (dep->method == kMethodDownload) {
        return remove_download(*proj, dep->name);
    }
    if (dep->method == kMethodApt) {
        return remove_apt(*proj, dep->name);
    }
    return std::unexpected(error::Error(std::format("unknown method: \"{}\"", dep->method)));
}

}
