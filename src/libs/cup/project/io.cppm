module;
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
export module cup.project:io;

import :config;
export import cup.error;

export namespace cup::project {

struct Project {
    std::filesystem::path root;
    Config config;

    [[nodiscard]] std::filesystem::path src() const { return root / "src"; }

    [[nodiscard]] bool uses_modules() const { return config.uses_modules(); }

    [[nodiscard]] bool uses_make() const { return config.uses_make(); }

    template <typename... Parts>
    [[nodiscard]] std::filesystem::path path(const Parts&... parts) const {
        std::filesystem::path joined = root;
        ((joined /= parts), ...);
        return joined;
    }
};

[[nodiscard]] std::string to_toml(const Config& cfg);

[[nodiscard]] std::expected<Config, error::Error> parse_config(std::string_view text);

[[nodiscard]] std::expected<void, error::Error> write_config(const std::filesystem::path& root,
                                                             const Config& cfg);

[[nodiscard]] std::expected<Project, error::Error> find_from(const std::filesystem::path& start);

[[nodiscard]] std::expected<Project, error::Error> find();

}
