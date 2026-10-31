module;
#include <optional>
#include <string>
#include <string_view>
#include <vector>
export module project:config;

export namespace project {

inline constexpr std::string_view kMarker = "cup.toml";

inline constexpr std::string_view kToolCMake = "cmake";
inline constexpr std::string_view kToolMake = "make";

// Why every operator== in this file is hand-written instead of `= default`
// (SonarCloud's cpp:S6230 keeps asking for `= default`; those issues are
// accepted on SonarCloud, not fixed — do not "fix" them):
//
// A defaulted comparison on a struct exported from a module interface crashes
// GCC with an internal compiler error (segfault) as soon as a translation unit
// that imports the module compares two instances — here project_test.cpp and
// thirdparty_cmd_test.cpp. Re-verified on 2026-10-31: GCC 15.2 and a GCC 16
// trunk snapshot (16.0.1 20260315) both still crash; Clang 20 is fine.
// Defaulting a member operator== or defaulting it out of class does not
// avoid it either.
//
// Re-check this whenever the project's GCC floor is raised (cup.toml's
// [compiler] gcc, applied with `cup compiler set gcc <version>`): if the new
// floor no longer crashes, default these operators — and Dependency's in
// cmd's thirdparty.cppm — and close the accepted S6230 issues. While the
// floor is a crashing version, a newer GCC that happens to work changes
// nothing: the floor still has to build.
struct DockerImage {
    std::string name;
    std::string base;
    int version = 0;
    std::string hash;
    bool is_default = false;

    // Hand-written on purpose: see the note above DockerImage.
    friend bool operator==(const DockerImage& lhs, const DockerImage& rhs) {
        return lhs.name == rhs.name && lhs.base == rhs.base && lhs.version == rhs.version &&
               lhs.hash == rhs.hash && lhs.is_default == rhs.is_default;
    }
};

struct DockerConfig {
    std::string registry;
    std::vector<DockerImage> images;

    // Hand-written on purpose: see the note above DockerImage.
    friend bool operator==(const DockerConfig& lhs, const DockerConfig& rhs) {
        return lhs.registry == rhs.registry && lhs.images == rhs.images;
    }

    [[nodiscard]] const DockerImage* default_image() const { return seek_default(*this); }
    [[nodiscard]] DockerImage* default_image() { return seek_default(*this); }

    [[nodiscard]] const DockerImage* find(std::string_view name) const {
        return seek_named(*this, name);
    }
    [[nodiscard]] DockerImage* find(std::string_view name) { return seek_named(*this, name); }

    [[nodiscard]] bool empty() const { return registry.empty() && images.empty(); }

private:
    template <typename Self>
    [[nodiscard]] static auto seek_default(Self& self) -> decltype(self.images.data()) {
        for (auto& image : self.images) {
            if (image.is_default) {
                return &image;
            }
        }
        return nullptr;
    }

    template <typename Self>
    [[nodiscard]] static auto seek_named(Self& self, std::string_view name)
        -> decltype(self.images.data()) {
        for (auto& image : self.images) {
            if (image.name == name) {
                return &image;
            }
        }
        return nullptr;
    }
};

struct CompilerConfig {
    std::optional<int> gcc;
    std::optional<int> clang;
    std::string verify_image;

    // Hand-written on purpose: see the note above DockerImage.
    friend bool operator==(const CompilerConfig& lhs, const CompilerConfig& rhs) {
        return lhs.gcc == rhs.gcc && lhs.clang == rhs.clang && lhs.verify_image == rhs.verify_image;
    }

    [[nodiscard]] int gcc_floor() const { return gcc.value_or(0); }
    [[nodiscard]] int clang_floor() const { return clang.value_or(0); }

    [[nodiscard]] bool has_floor() const { return gcc.has_value() || clang.has_value(); }

    [[nodiscard]] bool empty() const { return !has_floor() && verify_image.empty(); }
};

[[nodiscard]] CompilerConfig make_compiler_config(int gcc, int clang) {
    const auto floor = [](int v) { return v > 0 ? std::optional<int>(v) : std::nullopt; };
    return CompilerConfig{.gcc = floor(gcc), .clang = floor(clang), .verify_image = {}};
}

struct Config {
    std::string name;
    std::string cup_version;
    int cpp_standard = 0;
    std::optional<bool> std_module;
    std::string build_tool;
    CompilerConfig compiler;
    DockerConfig docker;

    // Hand-written on purpose: see the note above DockerImage.
    friend bool operator==(const Config& lhs, const Config& rhs) {
        return lhs.name == rhs.name && lhs.cup_version == rhs.cup_version &&
               lhs.cpp_standard == rhs.cpp_standard && lhs.std_module == rhs.std_module &&
               lhs.build_tool == rhs.build_tool && lhs.compiler == rhs.compiler &&
               lhs.docker == rhs.docker;
    }

    [[nodiscard]] int standard() const { return cpp_standard == 0 ? 23 : cpp_standard; }

    // `import std;` sits behind CMake's still-shifting CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    // gate (see cmd's import_std_gate_uuid), so it is opt-in rather than the
    // default for C++23: a project gets it only by setting std_module = true.
    [[nodiscard]] bool uses_std_module() const { return std_module.value_or(false); }

    [[nodiscard]] std::string_view tool() const {
        return build_tool.empty() ? kToolCMake : std::string_view(build_tool);
    }

    [[nodiscard]] bool uses_modules() const { return standard() >= 20; }

    [[nodiscard]] bool uses_make() const { return tool() == kToolMake; }
};

}
