module;
#include <charconv>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
module cup.cmd;

import cup.scaffold;
import cup.platform;
import cup.ui;

namespace cup::cmd {
namespace {

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::expected<void, error::Error> write_whole_file(const std::filesystem::path& path,
                                                    std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", path.string())));
    }
    return {};
}

}  // namespace

std::pair<int, int> effective_compilers(const project::Config& cfg) {
    if (cfg.compiler.has_floor()) {
        return {cfg.compiler.gcc_floor(), cfg.compiler.clang_floor()};
    }
    return scaffold::min_compilers(cfg.standard());
}

std::string floor_label(int v) {
    if (v == 0) {
        return "(no floor)";
    }
    return std::format(">= {}", v);
}

std::expected<void, error::Error> show_compilers(const project::Project& proj) {
    const auto [gcc, clang] = effective_compilers(proj.config);
    ui::accent("minimum compiler versions");
    ui::emit_line("  gcc     " + floor_label(gcc));
    ui::emit_line("  clang   " + floor_label(clang));
    std::string image = proj.config.compiler.verify_image;
    if (image.empty()) {
        image = "(unset — set verify_image in cup.toml or pass --image)";
    }
    ui::emit_line("  verify  " + image);
    return {};
}

std::expected<ParsedCompilerFlags, error::Error> parse_compiler_flags(
    std::span<const std::string> args) {
    ParsedCompilerFlags parsed;
    std::size_t i = 0;
    while (i < args.size()) {
        if (args[i] == "--no-verify") {
            parsed.no_verify = true;
            ++i;
        } else if (args[i] == "--image") {
            if (i + 1 >= args.size()) {
                return std::unexpected(error::Error("--image needs a docker image reference"));
            }
            parsed.image = args[i + 1];
            i += 2;
        } else {
            parsed.rest.push_back(args[i]);
            ++i;
        }
    }
    return parsed;
}

std::expected<PlannedCompilerChange, error::Error> plan_compiler_change(
    const project::Config& cur, std::span<const std::string> rest) {
    if (rest.size() != 2) {
        return std::unexpected(
            error::Error("usage: cup compiler set <gcc|clang> <version> [--image REF] [--no-verify]"));
    }
    const std::string& name = rest[0];
    if (name != "gcc" && name != "clang") {
        return std::unexpected(error::Error(std::format("unknown compiler \"{}\" (use: gcc, clang)", name)));
    }
    int ver = 0;
    const auto& text = rest[1];
    if (const auto parsed = std::from_chars(text.data(), text.data() + text.size(), ver);
        parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || ver < 0) {
        return std::unexpected(error::Error(
            std::format("invalid version \"{}\": want a non-negative major version like 15", text)));
    }

    auto [gcc, clang] = effective_compilers(cur);
    if (name == "gcc") {
        gcc = ver;
    } else {
        clang = ver;
    }
    project::Config cfg = cur;
    cfg.compiler = project::make_compiler_config(gcc, clang);
    cfg.compiler.verify_image = cur.compiler.verify_image;  // preserve the verify image
    return PlannedCompilerChange{.name = name, .version = ver, .config = std::move(cfg)};
}

bool has_verify_target(const project::Project& proj, std::string_view image) {
    if (!image.empty()) {
        return true;
    }
    if (proj.config.docker.default_image() != nullptr) {
        return true;
    }
    return !proj.config.compiler.verify_image.empty();
}

std::expected<std::string, error::Error> resolve_verify_image(const project::Project& proj,
                                                               std::string_view override_image) {
    if (!override_image.empty()) {
        return std::string(override_image);
    }
    if (const auto* def = proj.config.docker.default_image(); def != nullptr) {
        if (auto synced = sync_default_build_image(proj); !synced.has_value()) {
            return std::unexpected(std::move(synced).error());
        }
        const std::string tag = def->name + ":latest";
        ui::running("building " + tag + " from " + def->base);
        if (auto built = run_shell(proj.root, "docker",
                                   std::vector<std::string>{"build", "-t", tag,
                                                            docker_image_dir(proj, def->name).string()});
            !built.has_value()) {
            return std::unexpected(std::move(built).error());
        }
        return tag;
    }
    if (!proj.config.compiler.verify_image.empty()) {
        return proj.config.compiler.verify_image;
    }
    return std::unexpected(error::Error(
        "no docker image to verify against: create a build image with `cup docker new`, "
        "set verify_image in cup.toml, or pass --image REF"));
}

std::expected<void, error::Error> docker_verify(const project::Project& proj,
                                                std::string_view override_image) {
    auto image = resolve_verify_image(proj, override_image);
    if (!image.has_value()) {
        return std::unexpected(std::move(image).error());
    }
    ui::running("compiling the project in " + *image);
    constexpr std::string_view kScript = "cmake -S /work -B /tmp/cup-verify -G Ninja && "
                                         "cmake --build /tmp/cup-verify";
    const std::vector<std::string> args{"run", "--rm", "-v", proj.root.string() + ":/work:ro", *image,
                                        "sh", "-c", std::string(kScript)};
    return run_shell(proj.root, "docker", args);
}

std::expected<void, error::Error> apply_compiler_floor(const std::filesystem::path& root,
                                                        const project::Config& cfg) {
    if (auto wrote = project::write_config(root, cfg); !wrote.has_value()) {
        return wrote;
    }
    ui::updated(std::string(project::kMarker) + "  (compiler floor)");
    return scaffold::replace_compiler_guard(root, root / "CMakeLists.txt", cfg.compiler.gcc_floor(),
                                            cfg.compiler.clang_floor());
}

std::expected<void, error::Error> commit_compiler_floor(const project::Project& proj,
                                                         const project::Config& cfg,
                                                         std::string_view image, bool no_verify) {
    const std::filesystem::path toml_path = proj.path(project::kMarker);
    const std::filesystem::path cmake_path = proj.path("CMakeLists.txt");
    const auto old_toml = read_whole_file(toml_path);
    const auto old_cmake = read_whole_file(cmake_path);
    if (!old_toml.has_value() || !old_cmake.has_value()) {
        return std::unexpected(error::Error("cannot snapshot project files before changing the compiler floor"));
    }
    const auto restore = [&] {
        static_cast<void>(write_whole_file(toml_path, *old_toml));
        static_cast<void>(write_whole_file(cmake_path, *old_cmake));
    };

    // A partial write (e.g. cup.toml updated but the CMakeLists has no guard
    // markers to rewrite) must not leave the two files disagreeing.
    if (auto applied = apply_compiler_floor(proj.root, cfg); !applied.has_value()) {
        restore();
        return applied;
    }
    if (no_verify) {
        ui::skipped("docker verification (--no-verify)");
        return {};
    }

    if (auto verified = docker_verify(proj, image); !verified.has_value()) {
        restore();
        ui::err("compiler change cancelled: the project did not compile on the verify image");
        return verified;
    }
    return {};
}

std::expected<void, error::Error> set_compiler(const project::Project& proj,
                                               std::span<const std::string> args) {
    auto flags = parse_compiler_flags(args);
    if (!flags.has_value()) {
        return std::unexpected(std::move(flags).error());
    }
    auto planned = plan_compiler_change(proj.config, flags->rest);
    if (!planned.has_value()) {
        return std::unexpected(std::move(planned).error());
    }

    if (!flags->no_verify && !has_verify_target(proj, flags->image)) {
        return std::unexpected(error::Error(
            "no docker image to verify against: create a build image with `cup docker new`, "
            "set verify_image in cup.toml, pass --image REF, or use --no-verify to skip the check"));
    }

    if (auto committed = commit_compiler_floor(proj, planned->config, flags->image, flags->no_verify);
        !committed.has_value()) {
        return committed;
    }
    const std::string_view suffix = flags->no_verify ? " (unverified)." : ".";
    ui::success(std::format("done — {} minimum is now {}{}", planned->name,
                            floor_label(planned->version), suffix));
    return {};
}

std::expected<void, error::Error> verify_compiler(const project::Project& proj,
                                                   std::span<const std::string> args) {
    auto flags = parse_compiler_flags(args);
    if (!flags.has_value()) {
        return std::unexpected(std::move(flags).error());
    }
    if (!flags->rest.empty()) {
        return std::unexpected(error::Error("usage: cup compiler verify [--image REF]"));
    }
    if (auto verified = docker_verify(proj, flags->image); !verified.has_value()) {
        return verified;
    }
    ui::success("ok — the project compiles on the verify image.");
    return {};
}

std::expected<void, error::Error> run_compiler(std::span<const std::string> args) {
    auto proj = project::find();
    if (!proj.has_value()) {
        return std::unexpected(std::move(proj).error());
    }
    if (args.empty() || args[0] == "show") {
        return show_compilers(*proj);
    }
    const std::string& sub = args[0];
    const std::span<const std::string> rest = args.subspan(1);
    if (sub == "set") {
        return set_compiler(*proj, rest);
    }
    if (sub == "verify") {
        return verify_compiler(*proj, rest);
    }
    return std::unexpected(
        error::Error(std::format("unknown `cup compiler` subcommand \"{}\" (use: show, set, verify)", sub)));
}

}
