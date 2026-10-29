module;
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <cup/version.h>
module cup.cmd;

import cup.project;
import cup.scaffold;
import cup.ui;

namespace cup::cmd {
namespace {

// cup's own version, stamped into the new cup.toml — see New.cpp's kCupVersion.
inline constexpr std::string_view kCupVersion = CUP_VERSION;

constexpr std::string_view kCMakeLists = "CMakeLists.txt";

// Directories never worth scanning for a project's own CMakeLists: build
// trees, vendored dependencies, and dot-directories (.git, .cache, ...).
// Their CMakeLists say what *someone else's* code needs, not this project.
bool skipped_dir(const std::filesystem::path& dir) {
    const std::string name = dir.filename().string();
    if (name.empty() || name.front() == '.') {
        return true;
    }
    static constexpr std::array<std::string_view, 8> kSkip{
        "build", "_deps", "third_party", "thirdparty", "external", "extern", "vendor", "deps"};
    return std::ranges::find(kSkip, name) != kSkip.end() || name.starts_with("build-") ||
           name.starts_with("cmake-build-");
}

// Bounds the scan so a huge tree (a monorepo, a vendored SDK under an
// unexpected name) can't make `cup init` crawl.
constexpr std::size_t kMaxScannedFiles = 256;

std::optional<std::string> slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The project's own CMakeLists.txt files: the root one, then every one below
// it outside skipped_dir trees, up to kMaxScannedFiles.
std::vector<std::filesystem::path> project_cmakelists(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out{root / kCMakeLists};
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (out.size() >= kMaxScannedFiles) {
            break;
        }
        if (it->is_directory(ec)) {
            if (skipped_dir(it->path())) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->path().filename() == kCMakeLists && it->path().parent_path() != root) {
            out.push_back(it->path());
        }
    }
    return out;
}

// max_standard folds every regex match's first capture through
// parse_std_token, keeping the newest supported standard.
int max_standard(std::string_view text, const std::regex& re) {
    int best = 0;
    const std::string s(text);
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator();
         ++it) {
        if (const auto std = parse_std_token((*it)[1].str()); std.has_value()) {
            best = std::max(best, *std);
        }
    }
    return best;
}

// -std=c++17, -std=gnu++2a, /std:c++20 (MSVC) — shared by both detectors.
const std::regex& std_flag_re() {
    static const std::regex re(R"([-/]std[=:](?:c|gnu)\+\+([0-9][0-9a-z]))", std::regex::icase);
    return re;
}

// strip_comments drops every '#' comment (CMake and Make share the syntax),
// so a commented-out `# set(CMAKE_CXX_STANDARD 11)` doesn't count.
std::string strip_comments(std::string_view text) {
    std::string code;
    code.reserve(text.size());
    bool in_comment = false;
    for (const char c : text) {
        if (c == '\n') {
            in_comment = false;
        } else if (c == '#') {
            in_comment = true;
        }
        if (!in_comment) {
            code += c;
        }
    }
    return code;
}

// has_build_file reports whether root already holds tool's build file.
bool has_build_file(const std::filesystem::path& root, std::string_view tool) {
    std::error_code ec;
    if (tool == project::kToolCMake) {
        return std::filesystem::is_regular_file(root / kCMakeLists, ec);
    }
    return std::ranges::any_of(std::array<std::string_view, 3>{"GNUmakefile", "makefile", "Makefile"},
                               [&](std::string_view f) {
                                   return std::filesystem::is_regular_file(root / f, ec);
                               });
}

// InitOptions is `cup init`'s parsed command line.
struct InitOptions {
    std::string name;
    std::optional<int> standard;
    std::string build_tool;
};

std::expected<InitOptions, error::Error> parse_init_args(std::span<const std::string> args) {
    InitOptions opts;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];
        std::string value;
        const auto take_value = [&](std::string_view flag) -> std::expected<void, error::Error> {
            if (arg.size() > flag.size() && arg[flag.size()] == '=') {
                value = std::string(arg.substr(flag.size() + 1));
                return {};
            }
            if (i + 1 >= args.size()) {
                return std::unexpected(error::Error(std::format("{} needs a value", flag)));
            }
            value = args[++i];
            return {};
        };

        if (arg == "--std" || arg.starts_with("--std=")) {
            if (auto took = take_value("--std"); !took.has_value()) {
                return std::unexpected(std::move(took).error());
            }
            auto parsed = scaffold::parse_std(value);
            if (!parsed.has_value()) {
                return std::unexpected(std::move(parsed).error());
            }
            opts.standard = *parsed;
        } else if (arg == "--build-tool" || arg.starts_with("--build-tool=")) {
            if (auto took = take_value("--build-tool"); !took.has_value()) {
                return std::unexpected(std::move(took).error());
            }
            if (value != project::kToolCMake && value != project::kToolMake) {
                return std::unexpected(error::Error(std::format(
                    "--build-tool \"{}\": must be {} or {}", value, project::kToolCMake,
                    project::kToolMake)));
            }
            opts.build_tool = value;
        } else if (arg.starts_with("-")) {
            return std::unexpected(error::Error(std::format("unknown flag: {}", arg)));
        } else if (opts.name.empty()) {
            opts.name = std::string(arg);
        } else {
            return std::unexpected(error::Error(std::format("unexpected argument: {}", arg)));
        }
    }
    return opts;
}

// resolve_name picks the project name: an explicit argument (validated, as
// `cup new` does), else the detected project() name, else the directory's
// own name made into an identifier — prompting only if none of those is a
// valid identifier.
std::expected<std::string, error::Error> resolve_name(const InitOptions& opts,
                                                      const Detected& detected,
                                                      const std::filesystem::path& root) {
    if (!opts.name.empty()) {
        return resolve_project_name(std::span<const std::string>(&opts.name, 1));
    }
    for (const std::string& candidate :
         {detected.name, sanitize_ident(root.filename().string())}) {
        if (!candidate.empty() && scaffold::validate_ident(candidate).has_value()) {
            ui::detected(std::format("project name \"{}\"", candidate));
            return candidate;
        }
    }
    return resolve_project_name({});
}

std::expected<int, error::Error> resolve_standard(const InitOptions& opts, const Detected& detected,
                                                  std::string_view tool) {
    int std = opts.standard.value_or(detected.standard);
    if (std == 0) {
        auto chosen = choose_standard(tool);
        if (!chosen.has_value()) {
            return std::unexpected(std::move(chosen).error());
        }
        std = *chosen;
    } else if (!opts.standard.has_value()) {
        ui::detected(scaffold::std_label(std));
    }
    // cup's Make backend is the headers family only (see standard_choices):
    // it scaffolds no modules, so a C++20/23 project has to come in as CMake.
    if (tool == project::kToolMake && scaffold::uses_modules(std)) {
        return std::unexpected(error::Error(std::format(
            "{} with make: cup's Make backend supports C++11/14/17 only — pass "
            "--std 17 (or lower), or use --build-tool cmake",
            scaffold::std_label(std))));
    }
    return std;
}

// wire_cmake hooks an existing CMake build into cup without restructuring
// it: an empty compiler-guard block (so `cup compiler set` works), and the
// src/libs + src/apps directories `cup add` registers into — but only where
// they don't already exist, so an existing src/ layout is never wired twice
// (a second add_subdirectory of the same directory is a CMake error).
std::expected<void, error::Error> wire_cmake(const project::Project& proj) {
    const auto& root = proj.root;
    const std::filesystem::path cmake = root / kCMakeLists;

    auto text = slurp(cmake);
    if (!text.has_value()) {
        return std::unexpected(error::Error(std::format("reading {}", cmake.string())));
    }
    if (text->contains(scaffold::kGuardStart)) {
        ui::skipped("CMakeLists.txt already has a compiler-guard block");
    } else if (auto guarded = with_compiler_guard(*text); guarded.has_value()) {
        std::ofstream out(cmake, std::ios::binary | std::ios::trunc);
        out << *guarded;
        if (!out) {
            return std::unexpected(error::Error(std::format("writing {}", cmake.string())));
        }
        ui::updated("CMakeLists.txt  (compiler guard)");
    } else {
        ui::skipped("CMakeLists.txt has no project() call to anchor a compiler guard to; "
                    "`cup compiler set` will need one");
    }

    // libs before apps: an app links the libs, so they are configured first.
    for (const std::string_view sub : {"libs", "apps"}) {
        const std::filesystem::path dir = proj.src() / sub;
        const std::string label = std::format("src/{}", sub);
        if (std::error_code ec; std::filesystem::exists(dir, ec)) {
            if (std::filesystem::exists(dir / kCMakeLists, ec)) {
                ui::skipped(std::format("{} already exists — `cup add` registers into its "
                                        "CMakeLists.txt",
                                        label));
            } else {
                ui::skipped(std::format("{} already exists without a CMakeLists.txt — add one "
                                        "(and add_subdirectory it) to use `cup add` there",
                                        label));
            }
            continue;
        }
        if (auto made = scaffold::ensure_file(root, dir / kCMakeLists, ""); !made.has_value()) {
            return made;
        }
        if (auto wired =
                scaffold::ensure_line(root, cmake, std::format("add_subdirectory({})", label));
            !wired.has_value()) {
            return wired;
        }
    }
    return {};
}

}  // namespace

std::optional<int> parse_std_token(std::string_view token) {
    std::string t(token);
    std::ranges::transform(t, t.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // The pre-standardisation spellings GCC/Clang accept for each.
    static constexpr std::array<std::pair<std::string_view, int>, 10> kTokens{{
        {"11", 11}, {"0x", 11}, {"14", 14}, {"1y", 14}, {"17", 17},
        {"1z", 17}, {"20", 20}, {"2a", 20}, {"23", 23}, {"2b", 23},
    }};
    for (const auto& [spelling, std] : kTokens) {
        if (t == spelling) {
            return std;
        }
    }
    return std::nullopt;
}

int detect_cmake_standard(std::string_view text) {
    // CMAKE_CXX_STANDARD 17 / CXX_STANDARD 17 (set_target_properties), quoted
    // or not; cxx_std_17 compile features; and raw -std= flags.
    static const std::regex standard_var(R"(CXX_STANDARD["\s]+"?([0-9][0-9a-z]))",
                                         std::regex::icase);
    static const std::regex feature(R"(cxx_std_([0-9][0-9]))", std::regex::icase);
    const std::string code = strip_comments(text);
    return std::max({max_standard(code, standard_var), max_standard(code, feature),
                     max_standard(code, std_flag_re())});
}

int detect_make_standard(std::string_view text) {
    return max_standard(strip_comments(text), std_flag_re());
}

std::string detect_cmake_name(std::string_view text) {
    static const std::regex project_call(R"(^[ \t]*project[ \t]*\([ \t\r\n]*([A-Za-z_][A-Za-z0-9_.+-]*))",
                                         std::regex::icase | std::regex::multiline);
    const std::string s(text);
    std::smatch m;
    if (std::regex_search(s, m, project_call)) {
        return m[1].str();
    }
    return "";
}

std::string sanitize_ident(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + 1);
    for (const char c : raw) {
        const auto u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) != 0 || c == '_') ? c : '_';
    }
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::optional<std::string> with_compiler_guard(std::string_view text) {
    static const std::regex project_call(R"(^[ \t]*project[ \t]*\()",
                                         std::regex::icase | std::regex::multiline);
    const std::string s(text);
    std::smatch m;
    if (!std::regex_search(s, m, project_call)) {
        return std::nullopt;
    }
    // project(...) may span several lines; the guard goes after the line
    // holding its closing parenthesis: the first ')' outside a quoted
    // argument (DESCRIPTION "a tool (fast)" holds one that isn't).
    const auto open = static_cast<std::size_t>(m.position(0) + m.length(0));
    std::size_t close = std::string::npos;
    bool quoted = false;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (quoted && s[i] == '\\') {
            ++i;  // an escaped character, \" included
        } else if (s[i] == '"') {
            quoted = !quoted;
        } else if (!quoted && s[i] == ')') {
            close = i;
            break;
        }
    }
    if (close == std::string::npos) {
        return std::nullopt;
    }
    auto eol = s.find('\n', close);
    eol = eol == std::string::npos ? s.size() : eol + 1;
    std::string out = s.substr(0, eol);
    if (!out.empty() && out.back() != '\n') {
        out += '\n';
    }
    out += '\n';
    out += scaffold::compiler_guard(0, 0);
    out += "\n\n";
    out += s.substr(eol);
    return out;
}

Detected detect_project(const std::filesystem::path& root) {
    Detected d;
    std::error_code ec;
    if (std::filesystem::is_regular_file(root / kCMakeLists, ec)) {
        d.build_tool = std::string(project::kToolCMake);
        const auto root_text = slurp(root / kCMakeLists).value_or("");
        d.name = detect_cmake_name(root_text);
        for (const auto& file : project_cmakelists(root)) {
            d.standard = std::max(d.standard, detect_cmake_standard(slurp(file).value_or("")));
        }
        return d;
    }
    for (const std::string_view makefile : {"GNUmakefile", "makefile", "Makefile"}) {
        if (std::filesystem::is_regular_file(root / makefile, ec)) {
            d.build_tool = std::string(project::kToolMake);
            d.standard = detect_make_standard(slurp(root / makefile).value_or(""));
            return d;
        }
    }
    return d;
}

std::expected<void, error::Error> run_init(std::span<const std::string> args) {
    auto opts = parse_init_args(args);
    if (!opts.has_value()) {
        return std::unexpected(std::move(opts).error());
    }

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::current_path(ec);
    if (ec) {
        return std::unexpected(
            error::Error(std::format("getting the working directory: {}", ec.message())));
    }
    if (std::filesystem::exists(root / project::kMarker, ec)) {
        return std::unexpected(error::Error(std::format(
            "{} already exists — this directory is already a cup project", project::kMarker)));
    }

    const Detected detected = detect_project(root);

    std::string tool = opts->build_tool;
    if (tool.empty()) {
        tool = detected.build_tool;
    }
    if (tool.empty()) {
        auto chosen = choose_build_tool();
        if (!chosen.has_value()) {
            return std::unexpected(std::move(chosen).error());
        }
        tool = *std::move(chosen);
    } else if (opts->build_tool.empty()) {
        ui::detected(std::format("a {} project", tool));
    }

    auto name = resolve_name(*opts, detected, root);
    if (!name.has_value()) {
        return std::unexpected(std::move(name).error());
    }
    auto std_choice = resolve_standard(*opts, detected, tool);
    if (!std_choice.has_value()) {
        return std::unexpected(std::move(std_choice).error());
    }

    // No [compiler] floors and no build image: an adopted project has never
    // been checked against either, so cup claims neither. `cup compiler set`
    // and `cup docker new` add them once the user knows what it builds with.
    const project::Project proj{
        root,
        project::Config{
            .name = *name,
            .cup_version = std::string(kCupVersion),
            .cpp_standard = *std_choice,
            .build_tool = tool,
        },
    };
    if (auto wrote = project::write_config(root, proj.config); !wrote.has_value()) {
        return std::unexpected(std::move(wrote).error());
    }
    ui::wrote(project::kMarker);

    // Adopting a project whose build file cup didn't write: wire into it.
    // Without one, scaffold the same tree `cup new` would.
    const bool adopting = has_build_file(root, tool);
    const std::string_view fam = proj.uses_make() ? project::kToolMake
                                                  : scaffold::family(*std_choice);
    if (!adopting) {
        if (auto tree = scaffold_project_tree(proj, *std_choice, 0, 0); !tree.has_value()) {
            return tree;
        }
    } else {
        if (proj.uses_make()) {
            ui::skipped("Makefile left as is — `cup build`/`cup test` run `make MODE=<mode>` "
                        "and `make MODE=<mode> test`");
        } else if (auto wired = wire_cmake(proj); !wired.has_value()) {
            return wired;
        }
        auto gitignore = scaffold::render(root, fam, "project", "gitignore.tmpl", {});
        if (!gitignore.has_value()) {
            return std::unexpected(std::move(gitignore).error());
        }
        if (auto merged = merge_gitignore(root, *gitignore); !merged.has_value()) {
            return merged;
        }
    }

    ui::success("done.");
    if (proj.uses_make() && adopting) {
        ui::next("cup build       # runs your Makefile (MODE=Debug)");
    } else {
        ui::next("cup add app     # scaffold an executable under src/apps");
        ui::next("cup build       # compile (Debug)");
    }
    ui::next("cup compiler set gcc <version>   # pin a minimum compiler (optional)");
    return {};
}

}
