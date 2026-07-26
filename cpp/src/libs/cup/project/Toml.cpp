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
#include <vector>
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
            default: {
                const auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || uc == 0x7f) {
                    constexpr std::string_view kHex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(uc >> 4) & 0xF]);
                    out.push_back(kHex[uc & 0xF]);
                } else {
                    out.push_back(c);
                }
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
    return error::require(node->value<T>(), "field " + std::string(key) + " has the wrong type")
        .transform([](T value) { return std::optional<T>(std::move(value)); });
}

// bind reads key into dst, leaving dst untouched when the key is absent.
//
// It is the piece that lets a whole table decode as one and_then chain: every field
// read has the same expected<void> shape, so the error path is written once at the
// end of the chain rather than repeated after each of the fourteen reads below.
//
// Leaving dst alone on an absent key is what the old `value_or(default)` amounted
// to — every default in Config is already the member's default initialiser — and it
// is also what keeps one helper enough for the optional fields: assigning the
// unwrapped value to an std::optional<T> dst engages it, and absence leaves the
// nullopt that std_module's tri-state depends on.
template <typename T, typename Field>
[[nodiscard]] std::expected<void, error::Error> bind(const toml::table& tbl, std::string_view key,
                                                     Field& dst) {
    return field<T>(tbl, key).transform([&dst](std::optional<T> value) {
        if (value.has_value()) {
            dst = *std::move(value);
        }
    });
}

// parse_table turns toml++'s exception into the error channel, so parsing is a step
// in a chain like every other.
[[nodiscard]] std::expected<toml::table, error::Error> parse_table(std::string_view text) {
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& e) {
        return std::unexpected(error::Error(std::string(e.description())));
    }
}

// read_compiler decodes the [compiler] table. A missing table is not an error: it
// leaves the defaults in place, which is what projects predating the table rely on.
[[nodiscard]] std::expected<void, error::Error> read_compiler(const toml::table& tbl,
                                                              CompilerConfig& out) {
    const auto* compiler = tbl["compiler"].as_table();
    if (compiler == nullptr) {
        return {};
    }
    return bind<int>(*compiler, "gcc", out.gcc)
        .and_then([&] { return bind<int>(*compiler, "clang", out.clang); })
        .and_then([&] { return bind<std::string>(*compiler, "verify_image", out.verify_image); });
}

// read_image decodes one [[docker.image]] entry.
[[nodiscard]] std::expected<DockerImage, error::Error> read_image(const toml::node& node) {
    const auto* entry = node.as_table();
    if (entry == nullptr) {
        return std::unexpected(error::Error("docker.image entries must be tables"));
    }
    DockerImage image;
    return bind<std::string>(*entry, "name", image.name)
        .and_then([&] { return bind<std::string>(*entry, "base", image.base); })
        .and_then([&] { return bind<int>(*entry, "version", image.version); })
        .and_then([&] { return bind<std::string>(*entry, "hash", image.hash); })
        // The TOML key stays "default"; the field cannot, since that is a keyword.
        .and_then([&] { return bind<bool>(*entry, "default", image.is_default); })
        .transform([&image] { return std::move(image); });
}

// read_docker decodes the [docker] table and the [[docker.image]] array under it.
[[nodiscard]] std::expected<void, error::Error> read_docker(const toml::table& tbl,
                                                            DockerConfig& out) {
    const auto* docker = tbl["docker"].as_table();
    if (docker == nullptr) {
        return {};
    }
    return bind<std::string>(*docker, "registry", out.registry).and_then([&] {
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

// read_file slurps a file whole, reporting nullopt if it cannot be opened.
[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// load_project reads and decodes the cup.toml sitting in dir, as one chain: require
// lifts an unreadable file into the error channel, transform_error names the file
// on whatever the decoder reports, and transform seats the result in a Project.
//
// The two errors stay worded differently on purpose — a file cup cannot read says
// only "reading <path>", while one it can read but not decode appends the decoder's
// complaint — so transform_error wraps the parse alone rather than the whole chain.
[[nodiscard]] std::expected<Project, error::Error> load_project(const std::filesystem::path& dir) {
    const std::filesystem::path marker = dir / kMarker;
    return error::require(read_file(marker), "reading " + marker.string())
        .and_then([&marker](const std::string& text) {
            return parse_config(text).transform_error([&marker](const error::Error& e) {
                return error::Error("reading " + marker.string() + ": " + e.message());
            });
        })
        .transform([&dir](Config cfg) { return Project{.root = dir, .config = std::move(cfg)}; });
}

// current_directory is getcwd in the error channel, so find() below is one chain.
[[nodiscard]] std::expected<std::filesystem::path, error::Error> current_directory() {
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) {
        return std::unexpected(error::Error("getting the working directory: " + ec.message()));
    }
    return cwd;
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
    return parse_table(text).and_then([](const toml::table& tbl) {
        Config cfg;
        return bind<std::string>(tbl, "name", cfg.name)
            .and_then([&] { return bind<std::string>(tbl, "cup_version", cfg.cup_version); })
            .and_then([&] { return bind<int>(tbl, "cpp_standard", cfg.cpp_standard); })
            // Left as nullopt when the key is absent — the whole point of the
            // tri-state; see bind on why one helper covers that too.
            .and_then([&] { return bind<bool>(tbl, "std_module", cfg.std_module); })
            .and_then([&] { return bind<std::string>(tbl, "build_tool", cfg.build_tool); })
            .and_then([&] { return read_compiler(tbl, cfg.compiler); })
            .and_then([&] { return read_docker(tbl, cfg.docker); })
            .transform([&cfg] { return std::move(cfg); });
    });
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
    for (std::filesystem::path dir = start;;) {
        std::error_code ec;
        if (std::filesystem::exists(dir / kMarker, ec)) {
            return load_project(dir);
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

std::expected<Project, error::Error> find() { return current_directory().and_then(find_from); }

}  // namespace cup::project
