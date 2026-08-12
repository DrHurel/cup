module;
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
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
    const std::vector<std::string> args{std::format("MODE={}", mode)};
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
    const std::vector<std::string> args{
        "-G", "Ninja",
        std::format("-DCMAKE_BUILD_TYPE={}", mode),
        "-S", proj.root.string(),
        "-B", build_dir(proj, mode).string(),
    };
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

std::expected<void, error::Error> run_configure(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto mode = parse_mode(args).first;
    return configure(*proj, mode);
}

std::expected<void, error::Error> run_build(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto mode = parse_mode(args).first;
    return build(*proj, mode);
}

std::expected<void, error::Error> run_test(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    const auto mode = parse_mode(args).first;
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
