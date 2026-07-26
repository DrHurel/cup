// Implementation unit for cup.project — the TOML codec and the project walk.
//
// This is a module implementation unit (`module cup.project;`, no `export`), not
// an interface unit, and that is deliberate: a module implementation unit's global
// module fragment never becomes part of any BMI, so toml++ is compiled here once
// and seen by nothing else. Putting it in :io's fragment instead makes GCC 14 ICE
// while merging the partition — see the note at the top of io.cppm.
module;
#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
module cup.project;

namespace cup::project {
namespace {

// quote renders s as a TOML basic string, escaping exactly what Go's
// BurntSushi/toml encoder escapes so a rewritten cup.toml is byte-identical to one
// the Go cup would have written.
[[nodiscard]] std::string quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\f': out += "\\f"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    constexpr std::string_view kHex = "0123456789ABCDEF";
                    out += "\\u00";
                    out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
                    out.push_back(kHex[static_cast<unsigned char>(c) & 0xF]);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

// key_string emits `<indent><key> = "<value>"`.
void key_string(std::string& out, std::string_view indent, std::string_view key,
                std::string_view value) {
    out.append(indent).append(key).append(" = ").append(quote(value)).append("\n");
}

// key_int emits `<indent><key> = <value>`.
void key_int(std::string& out, std::string_view indent, std::string_view key, int value) {
    out.append(indent).append(key).append(" = ").append(std::to_string(value)).append("\n");
}

// key_bool emits `<indent><key> = true|false`.
void key_bool(std::string& out, std::string_view indent, std::string_view key, bool value) {
    out.append(indent).append(key).append(" = ").append(value ? "true" : "false").append("\n");
}

// field reads an optional key of type T, distinguishing "absent" from "present but
// the wrong type" — Go's decoder errors on the latter, so this reports it rather
// than silently falling back to the default.
template <typename T>
[[nodiscard]] std::expected<std::optional<T>, error::Error> field(const toml::table& tbl,
                                                                  std::string_view key) {
    const auto* node = tbl.get(key);
    if (node == nullptr) {
        return std::optional<T>{};
    }
    const auto value = node->value<T>();
    if (!value.has_value()) {
        return std::unexpected(error::Error("field " + std::string(key) + " has the wrong type"));
    }
    return std::optional<T>(*value);
}

// read_file slurps a file whole, reporting nullopt if it cannot be opened.
[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

std::string to_toml(const Config& cfg) {
    std::string out;

    // name and cpp_standard carry no omitempty in the Go struct, so they are
    // written unconditionally — including `name = ""` for a zero Config.
    key_string(out, "", "name", cfg.name);
    if (!cfg.cup_version.empty()) {
        key_string(out, "", "cup_version", cfg.cup_version);
    }
    key_int(out, "", "cpp_standard", cfg.cpp_standard);
    if (cfg.std_module.has_value()) {
        // The tri-state field. Writing it only when set is what lets an explicit
        // `std_module = false` survive a rewrite instead of collapsing to "unset"
        // and flipping the project onto `import std;`.
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
    toml::table tbl;
    try {
        tbl = toml::parse(text);
    } catch (const toml::parse_error& e) {
        return std::unexpected(error::Error(std::string(e.description())));
    }

    Config cfg;

    // Each field has a different type and target, so they are read out longhand;
    // a loop would only move the repetition somewhere less obvious.
    const auto name = field<std::string>(tbl, "name");
    if (!name) {
        return std::unexpected(name.error());
    }
    cfg.name = name->value_or("");

    const auto cup_version = field<std::string>(tbl, "cup_version");
    if (!cup_version) {
        return std::unexpected(cup_version.error());
    }
    cfg.cup_version = cup_version->value_or("");

    const auto cpp_standard = field<int>(tbl, "cpp_standard");
    if (!cpp_standard) {
        return std::unexpected(cpp_standard.error());
    }
    cfg.cpp_standard = cpp_standard->value_or(0);

    // Left as nullopt when the key is absent — the whole point of the tri-state.
    const auto std_module = field<bool>(tbl, "std_module");
    if (!std_module) {
        return std::unexpected(std_module.error());
    }
    cfg.std_module = *std_module;

    const auto build_tool = field<std::string>(tbl, "build_tool");
    if (!build_tool) {
        return std::unexpected(build_tool.error());
    }
    cfg.build_tool = build_tool->value_or("");

    if (const auto* compiler = tbl["compiler"].as_table()) {
        const auto gcc = field<int>(*compiler, "gcc");
        if (!gcc) {
            return std::unexpected(gcc.error());
        }
        cfg.compiler.gcc = *gcc;

        const auto clang = field<int>(*compiler, "clang");
        if (!clang) {
            return std::unexpected(clang.error());
        }
        cfg.compiler.clang = *clang;

        const auto verify_image = field<std::string>(*compiler, "verify_image");
        if (!verify_image) {
            return std::unexpected(verify_image.error());
        }
        cfg.compiler.verify_image = verify_image->value_or("");
    }

    if (const auto* docker = tbl["docker"].as_table()) {
        const auto registry = field<std::string>(*docker, "registry");
        if (!registry) {
            return std::unexpected(registry.error());
        }
        cfg.docker.registry = registry->value_or("");

        if (const auto* images = (*docker)["image"].as_array()) {
            for (const auto& node : *images) {
                const auto* entry = node.as_table();
                if (entry == nullptr) {
                    return std::unexpected(error::Error("docker.image entries must be tables"));
                }
                DockerImage image;

                const auto image_name = field<std::string>(*entry, "name");
                if (!image_name) {
                    return std::unexpected(image_name.error());
                }
                image.name = image_name->value_or("");

                const auto base = field<std::string>(*entry, "base");
                if (!base) {
                    return std::unexpected(base.error());
                }
                image.base = base->value_or("");

                const auto version = field<int>(*entry, "version");
                if (!version) {
                    return std::unexpected(version.error());
                }
                image.version = version->value_or(0);

                const auto hash = field<std::string>(*entry, "hash");
                if (!hash) {
                    return std::unexpected(hash.error());
                }
                image.hash = hash->value_or("");

                const auto is_default = field<bool>(*entry, "default");
                if (!is_default) {
                    return std::unexpected(is_default.error());
                }
                image.is_default = is_default->value_or(false);

                cfg.docker.images.push_back(std::move(image));
            }
        }
    }

    return cfg;
}

std::expected<void, error::Error> write_config(const std::filesystem::path& root,
                                               const Config& cfg) {
    const std::filesystem::path marker = root / kMarker;
    std::ofstream out(marker, std::ios::binary | std::ios::trunc);
    out << to_toml(cfg);
    if (!out) {
        return std::unexpected(error::Error("writing " + marker.string()));
    }
    return {};
}

std::expected<Project, error::Error> find_from(const std::filesystem::path& start) {
    std::filesystem::path dir = start;
    while (true) {
        const std::filesystem::path marker = dir / kMarker;
        std::error_code ec;
        if (std::filesystem::exists(marker, ec)) {
            const auto text = read_file(marker);
            if (!text) {
                return std::unexpected(error::Error("reading " + marker.string()));
            }
            auto cfg = parse_config(*text);
            if (!cfg) {
                return std::unexpected(
                    error::Error("reading " + marker.string() + ": " + cfg.error().message()));
            }
            return Project{.root = dir, .config = *std::move(cfg)};
        }
        // parent_path() of a root ("/") is itself, and of a bare relative name is
        // empty; either means the walk has run out of ancestors.
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir || parent.empty()) {
            return std::unexpected(error::Error("not inside a cup project (no " +
                                                std::string(kMarker) +
                                                " found in this or any parent directory)"));
        }
        dir = parent;
    }
}

std::expected<Project, error::Error> find() {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) {
        return std::unexpected(error::Error("getting the working directory: " + ec.message()));
    }
    return find_from(cwd);
}

}  // namespace cup::project
