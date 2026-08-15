module;
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
module cup.project;

namespace cup::project {
namespace {

// Mirrors cup.scaffold's kStandards (std.cppm): the set of C++ standards cup
// can scaffold. Duplicated rather than imported so this foundational,
// low-level module doesn't take on cup.scaffold's much heavier dependency
// graph (cup.ui, cup.tmpl, cup.platform) just to validate five integers.
inline constexpr std::array<int, 5> kStandards{23, 20, 17, 14, 11};

[[nodiscard]] std::expected<void, error::Error> check_cpp_standard(int cpp_standard) {
    if (std::ranges::find(kStandards, cpp_standard) == kStandards.end()) {
        return std::unexpected(error::Error(
            std::format("cpp_standard {}: not a supported standard (11, 14, 17, 20, 23)",
                        cpp_standard)));
    }
    return {};
}

[[nodiscard]] std::string quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"': out += R"(\")"; break;
            case '\\': out += R"(\\)"; break;
            case '\b': out += "\\b"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\f': out += "\\f"; break;
            case '\r': out += "\\r"; break;
            default: {
                const auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || uc == 0x7f) {
                    std::format_to(std::back_inserter(out), R"(\u00{:02x})", uc);
                } else {
                    out.push_back(c);
                }
            }
        }
    }
    out.push_back('"');
    return out;
}

void key_string(std::string& out, std::string_view indent, std::string_view key,
                std::string_view value) {
    out += std::format("{}{} = {}\n", indent, key, quote(value));
}

void key_int(std::string& out, std::string_view indent, std::string_view key, int value) {
    out += std::format("{}{} = {}\n", indent, key, value);
}

void key_bool(std::string& out, std::string_view indent, std::string_view key, bool value) {
    out += std::format("{}{} = {}\n", indent, key, value);
}

template <typename T>
[[nodiscard]] std::expected<std::optional<T>, error::Error> field(const toml::table& tbl,
                                                                  std::string_view key) {
    const auto* node = tbl.get(key);
    if (node == nullptr) {
        return std::optional<T>{};
    }
    return error::require(node->value<T>(), std::format("field {} has the wrong type", key))
        .transform([](T value) { return std::optional<T>(std::move(value)); });
}

template <typename T, typename Field>
[[nodiscard]] std::expected<void, error::Error> bind(const toml::table& tbl, std::string_view key,
                                                     Field& dst) {
    return field<T>(tbl, key).transform([&dst](std::optional<T> value) {
        if (value.has_value()) {
            dst = *std::move(value);
        }
    });
}

[[nodiscard]] std::expected<toml::table, error::Error> parse_table(std::string_view text) {
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& e) {
        return std::unexpected(error::Error(std::string(e.description())));
    }
}

[[nodiscard]] std::expected<void, error::Error> read_compiler(const toml::table& tbl,
                                                              CompilerConfig& out) {
    const auto* compiler = tbl["compiler"].as_table();
    if (compiler == nullptr) {
        return {};
    }
    return bind<int>(*compiler, "gcc", out.gcc)
        .and_then([compiler, &out] { return bind<int>(*compiler, "clang", out.clang); })
        .and_then([compiler, &out] {
            return bind<std::string>(*compiler, "verify_image", out.verify_image);
        });
}

[[nodiscard]] std::expected<DockerImage, error::Error> read_image(const toml::node& node) {
    const auto* entry = node.as_table();
    if (entry == nullptr) {
        return std::unexpected(error::Error("docker.image entries must be tables"));
    }
    DockerImage image;
    return bind<std::string>(*entry, "name", image.name)
        .and_then([entry, &image] { return bind<std::string>(*entry, "base", image.base); })
        .and_then([entry, &image] { return bind<int>(*entry, "version", image.version); })
        .and_then([entry, &image] { return bind<std::string>(*entry, "hash", image.hash); })
        .and_then([entry, &image] { return bind<bool>(*entry, "default", image.is_default); })
        .transform([&image] { return std::move(image); });
}

[[nodiscard]] std::expected<void, error::Error> read_docker(const toml::table& tbl,
                                                            DockerConfig& out) {
    const auto* docker = tbl["docker"].as_table();
    if (docker == nullptr) {
        return {};
    }
    return bind<std::string>(*docker, "registry", out.registry).and_then([docker, &out] {
        const auto* images = (*docker)["image"].as_array();
        if (images == nullptr) {
            return std::expected<void, error::Error>{};
        }
        return error::collect<DockerImage>(*images, read_image)
            .transform([&out](std::vector<DockerImage> decoded) {
                out.images = std::move(decoded);
            });
    });
}

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::expected<Project, error::Error> load_project(const std::filesystem::path& dir) {
    const std::filesystem::path marker = dir / kMarker;
    return error::require(read_file(marker), std::format("reading {}", marker.string()))
        .and_then([&marker](const std::string& text) {
            return parse_config(text).transform_error([&marker](const error::Error& e) {
                return error::Error(
                    std::format("reading {}: {}", marker.string(), e.message()));
            });
        })
        .transform([&dir](Config cfg) { return Project{.root = dir, .config = std::move(cfg)}; });
}

[[nodiscard]] std::expected<std::filesystem::path, error::Error> current_directory() {
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) {
        return std::unexpected(
            error::Error(std::format("getting the working directory: {}", ec.message())));
    }
    return cwd;
}

}

std::string to_toml(const Config& cfg) {
    std::string out;

    key_string(out, "", "name", cfg.name);
    if (!cfg.cup_version.empty()) {
        key_string(out, "", "cup_version", cfg.cup_version);
    }
    key_int(out, "", "cpp_standard", cfg.cpp_standard);
    if (cfg.std_module.has_value()) {
        key_bool(out, "", "std_module", *cfg.std_module);
    }
    if (!cfg.build_tool.empty()) {
        key_string(out, "", "build_tool", cfg.build_tool);
    }

    if (!cfg.compiler.empty()) {
        out += "\n[compiler]\n";
        if (cfg.compiler.gcc.has_value()) {
            key_int(out, "  ", "gcc", *cfg.compiler.gcc);
        }
        if (cfg.compiler.clang.has_value()) {
            key_int(out, "  ", "clang", *cfg.compiler.clang);
        }
        if (!cfg.compiler.verify_image.empty()) {
            key_string(out, "  ", "verify_image", cfg.compiler.verify_image);
        }
    }

    if (!cfg.docker.empty()) {
        out += "\n[docker]\n";
        if (!cfg.docker.registry.empty()) {
            key_string(out, "  ", "registry", cfg.docker.registry);
        }
        for (const auto& image : cfg.docker.images) {
            out += "\n  [[docker.image]]\n";
            key_string(out, "    ", "name", image.name);
            key_string(out, "    ", "base", image.base);
            key_int(out, "    ", "version", image.version);
            if (!image.hash.empty()) {
                key_string(out, "    ", "hash", image.hash);
            }
            if (image.is_default) {
                key_bool(out, "    ", "default", image.is_default);
            }
        }
    }

    return out;
}

std::expected<Config, error::Error> parse_config(std::string_view text) {
    return parse_table(text).and_then([](const toml::table& tbl) {
        Config cfg;
        return bind<std::string>(tbl, "name", cfg.name)
            .and_then(
                [&tbl, &cfg] { return bind<std::string>(tbl, "cup_version", cfg.cup_version); })
            .and_then([&tbl, &cfg] { return bind<int>(tbl, "cpp_standard", cfg.cpp_standard); })
            .and_then([&cfg] { return check_cpp_standard(cfg.cpp_standard); })
            .and_then([&tbl, &cfg] { return bind<bool>(tbl, "std_module", cfg.std_module); })
            .and_then(
                [&tbl, &cfg] { return bind<std::string>(tbl, "build_tool", cfg.build_tool); })
            .and_then([&tbl, &cfg] { return read_compiler(tbl, cfg.compiler); })
            .and_then([&tbl, &cfg] { return read_docker(tbl, cfg.docker); })
            .transform([&cfg] { return std::move(cfg); });
    });
}

std::expected<void, error::Error> write_config(const std::filesystem::path& root,
                                               const Config& cfg) {
    const std::filesystem::path marker = root / kMarker;
    std::ofstream out(marker, std::ios::binary | std::ios::trunc);
    out << to_toml(cfg);
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", marker.string())));
    }
    return {};
}

std::expected<Project, error::Error> find_from(const std::filesystem::path& start) {
    for (std::filesystem::path dir = start;;) {
        if (std::error_code ec; std::filesystem::exists(dir / kMarker, ec)) {
            return load_project(dir);
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir || parent.empty()) {
            return std::unexpected(error::Error(std::format(
                "not inside a cup project (no {} found in this or any parent directory)",
                kMarker)));
        }
        dir = parent;
    }
}

std::expected<Project, error::Error> find() { return current_directory().and_then(find_from); }

}
