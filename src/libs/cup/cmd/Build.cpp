module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
module cup.cmd;

import cup.ui;
import cup.scaffold;
import cup.platform;

namespace cup::cmd {
namespace {

struct ImportStdGate {
    int major;
    int minor;
    std::string_view uuid;
};

// CMake rotates CMAKE_EXPERIMENTAL_CXX_IMPORT_STD's expected value on almost
// every release, by design (Help/dev/experimental.rst: it forces a fresh read
// of the experimental-feature docs each time), and exposes no command to
// query it. cup carries the versions it has confirmed against that file;
// resolve_import_std_gate reports a clear error for anything outside it
// rather than letting the CMake configure fail deep inside CXX_MODULE_STD
// with a cryptic message.
const std::array kImportStdGates{
    ImportStdGate{3, 30, "0e5b6991-d74f-4b3d-a41c-cf096e0b2508"},
    ImportStdGate{3, 31, "0e5b6991-d74f-4b3d-a41c-cf096e0b2508"},
    ImportStdGate{4, 0, "a9e1cf81-9932-4810-974b-6eccaf14e457"},
    ImportStdGate{4, 1, "d0edc3af-4c50-42ea-a356-e2862fe7a444"},
    ImportStdGate{4, 2, "d0edc3af-4c50-42ea-a356-e2862fe7a444"},
    ImportStdGate{4, 3, "451f2fe2-a8a2-47c3-bc32-94786d8fc91b"},
    ImportStdGate{4, 4, "f35a9ac6-8463-4d38-8eec-5d6008153e7d"},
};

// Parses the "cmake version X.Y[.Z]" line `cmake --version` prints first.
std::optional<std::pair<int, int>> parse_cmake_major_minor(std::string_view text) {
    constexpr std::string_view kMarker = "cmake version ";
    const auto pos = text.find(kMarker);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest = text.substr(pos + kMarker.size());

    std::size_t i = 0;
    int major = 0;
    while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') {
        major = major * 10 + (rest[i] - '0');
        ++i;
    }
    if (i == 0 || i >= rest.size() || rest[i] != '.') {
        return std::nullopt;
    }
    ++i;

    const std::size_t minor_start = i;
    int minor = 0;
    while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') {
        minor = minor * 10 + (rest[i] - '0');
        ++i;
    }
    if (i == minor_start) {
        return std::nullopt;
    }
    return std::pair{major, minor};
}

// The first line of `cmake --version`'s output, for a readable error message.
std::string_view first_line(std::string_view text) {
    return text.substr(0, text.find('\n'));
}

std::string join(std::span<const std::string> args) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out += ' ';
        }
        out += args[i];
    }
    return out;
}

std::expected<void, error::Error> make_build(const project::Project& proj, std::string_view mode) {
    const std::vector args{std::format("MODE={}", mode)};
    return run_shell(proj.root, "make", args);
}

std::expected<void, error::Error> make_test(const project::Project& proj, std::string_view mode) {
    const std::vector<std::string> args{std::format("MODE={}", mode), "test"};
    return run_shell(proj.root, "make", args);
}

}  // namespace

// run_shell is cup.cmd's one call site reaching cup.platform::run_command,
// pairing it with the "run ..." status line the way Go's runCommand did in
// one step. cup.platform cannot log through cup.ui itself: cup.ui already
// imports cup.platform (is_tty / raw mode), so the reverse import would
// cycle. Exported (not file-local) so New.cpp/Add.cpp/Docker.cpp reuse the
// same logging wrapper instead of calling platform::run_command bare.
std::expected<void, error::Error> run_shell(const std::filesystem::path& dir, std::string_view name,
                                            std::span<const std::string> args) {
    ui::running(std::string(name) + " " + join(args));
    return platform::run_command(dir, name, args);
}

std::optional<std::string> import_std_gate_uuid(std::string_view cmake_version_output) {
    const auto parsed = parse_cmake_major_minor(cmake_version_output);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    const auto [major, minor] = *parsed;
    for (const auto& gate : kImportStdGates) {
        if (gate.major == major && gate.minor == minor) {
            return std::string(gate.uuid);
        }
    }
    return std::nullopt;
}

std::expected<std::string, error::Error> resolve_import_std_gate(const std::filesystem::path& dir) {
    const std::vector<std::string> version_args{"--version"};
    auto version_output = platform::capture_command(dir, "cmake", version_args);
    if (!version_output.has_value()) {
        return std::unexpected(std::move(version_output).error());
    }
    if (auto uuid = import_std_gate_uuid(*version_output); uuid.has_value()) {
        return *std::move(uuid);
    }
    return std::unexpected(error::Error(std::format(
        "{}: cup doesn't recognise this CMake release's `import std` gate value yet "
        "(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD, which CMake rotates almost every release). "
        "Set std_module = false in cup.toml to build without `import std;`, or update cup.",
        first_line(*version_output))));
}

std::pair<std::string, std::span<const std::string>> parse_mode(std::span<const std::string> args) {
    if (!args.empty()) {
        for (const auto& mode : kBuildModes) {
            if (args[0] == mode) {
                return {std::string(mode), args.subspan(1)};
            }
        }
    }
    return {"Debug", args};
}

std::filesystem::path build_dir(const project::Project& proj, std::string_view mode) {
    return proj.path("build", mode);
}

std::expected<void, error::Error> configure(const project::Project& proj, std::string_view mode) {
    if (proj.uses_make()) {
        ui::skipped("make needs no configure step");
        return {};
    }
    std::vector<std::string> args{
        "-G", "Ninja",
        std::format("-DCMAKE_BUILD_TYPE={}", mode),
    };
    // The gate must be set before CMakeLists.txt's project() call, so it goes
    // in as a -D rather than a committed `set()` — that also lets it track
    // whichever CMake is actually installed instead of going stale the moment
    // it differs from the one cup last hardcoded a value for.
    if (proj.config.uses_std_module()) {
        auto uuid = resolve_import_std_gate(proj.root);
        if (!uuid.has_value()) {
            return std::unexpected(std::move(uuid).error());
        }
        args.push_back(std::format("-DCMAKE_EXPERIMENTAL_CXX_IMPORT_STD={}", *uuid));
    }
    args.emplace_back("-S");
    args.push_back(proj.root.string());
    args.emplace_back("-B");
    args.push_back(build_dir(proj, mode).string());
    return run_shell(proj.root, "cmake", args);
}

std::expected<void, error::Error> build(const project::Project& proj, std::string_view mode) {
    if (proj.uses_make()) {
        return make_build(proj, mode);
    }
    if (auto configured = configure(proj, mode); !configured.has_value()) {
        return configured;
    }
    const std::vector<std::string> args{"--build", build_dir(proj, mode).string()};
    return run_shell(proj.root, "cmake", args);
}

// Neither run_configure/run_build/run_test nor rebuild/retest have any
// legitimate use for a leftover token after the mode — unlike run_run, whose
// `rest` carries the app name / `--`-separated program args. Silently
// dropping it would let a mistyped flag pass as if it were accepted.
std::expected<void, error::Error> reject_trailing_args(std::span<const std::string> rest) {
    if (!rest.empty()) {
        return std::unexpected(
            error::Error(std::format("unexpected argument(s): {}", join(rest))));
    }
    return {};
}

std::expected<void, error::Error> run_configure(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto [mode, rest] = parse_mode(args);
    if (auto checked = reject_trailing_args(rest); !checked.has_value()) {
        return checked;
    }
    return configure(*proj, mode);
}

std::expected<void, error::Error> run_build(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto [mode, rest] = parse_mode(args);
    if (auto checked = reject_trailing_args(rest); !checked.has_value()) {
        return checked;
    }
    return build(*proj, mode);
}

std::expected<void, error::Error> run_test(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto [mode, rest] = parse_mode(args);
    if (auto checked = reject_trailing_args(rest); !checked.has_value()) {
        return checked;
    }
    if (proj->uses_make()) {
        return make_test(*proj, mode);
    }
    if (auto built = build(*proj, mode); !built.has_value()) {
        return built;
    }
    const std::vector<std::string> args_ctest{"--test-dir", build_dir(*proj, mode).string(),
                                               "--output-on-failure"};
    return run_shell(proj->root, "ctest", args_ctest);
}

std::expected<std::pair<std::string, std::vector<std::string>>, error::Error> resolve_app(
    const project::Project& proj, std::span<const std::string> rest) {
    const std::vector<std::string> apps = scaffold::list_subdirs(proj.src() / "apps");
    if (apps.empty()) {
        return std::unexpected(error::Error("no apps to run (add one with `cup add app`)"));
    }

    std::string app_name;
    std::size_t consumed = 0;
    if (!rest.empty() && rest[0] != "--") {
        app_name = rest[0];
        consumed = 1;
        if (std::ranges::find(apps, app_name) == apps.end()) {
            return std::unexpected(error::Error(
                std::format("no such app \"{}\" (available: {})", app_name, join(apps))));
        }
    } else if (apps.size() == 1) {
        app_name = apps[0];
    } else {
        auto chosen = ui::select_one("which app?", apps, apps[0]);
        if (!chosen.has_value()) {
            return std::unexpected(std::move(chosen).error());
        }
        app_name = *std::move(chosen);
    }

    std::vector<std::string> remaining(rest.begin() + static_cast<std::ptrdiff_t>(consumed), rest.end());
    if (!remaining.empty() && remaining[0] == "--") {
        remaining.erase(remaining.begin());
    }
    return std::pair{std::move(app_name), std::move(remaining)};
}

std::expected<void, error::Error> run_run(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto [mode, rest] = parse_mode(args);

    auto resolved = resolve_app(*proj, rest);
    if (!resolved.has_value()) {
        return std::unexpected(std::move(resolved).error());
    }
    auto& [app_name, prog_args] = *resolved;

    if (auto built = build(*proj, mode); !built.has_value()) {
        return built;
    }

    const std::filesystem::path bin = build_dir(*proj, mode) / "bin" / app_name;
    return run_shell(proj->root, bin.string(), prog_args);
}

std::expected<void, error::Error> run_clean(std::span<const std::string> /*args*/) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    std::error_code ec;
    std::filesystem::remove_all(proj->path("build"), ec);
    if (ec) {
        return std::unexpected(error::Error(std::format("remove build/: {}", ec.message())));
    }
    ui::removed("build/");
    return {};
}

std::expected<void, error::Error> run_rebuild(std::span<const std::string> args) {
    if (auto cleaned = run_clean(std::span<const std::string>{}); !cleaned.has_value()) {
        return cleaned;
    }
    return run_build(args);
}

std::expected<void, error::Error> run_retest(std::span<const std::string> args) {
    if (auto cleaned = run_clean(std::span<const std::string>{}); !cleaned.has_value()) {
        return cleaned;
    }
    return run_test(args);
}

}
