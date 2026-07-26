module;
// Light headers only, and deliberately so. GCC 14 cannot merge two partitions of
// the same module that each drag in the heavier parts of the standard library —
// :io carries toml++, <filesystem> and <fstream>, and when this partition also
// included <filesystem> and <algorithm> the primary interface unit failed to read
// its own partition's BMI:
//
//     cup.project:io: error: failed to read compiled module cluster N: Bad file data
//     fatal error: failed to load pendings for 'std::_Mutex_base'
//
// That is the same failure ui.cppm documents for <print>/<format>/<iostream>, one
// layer down: it is not those three headers specifically, but any two partitions
// reaching the shared <memory>/<mutex> machinery underneath them. So the data
// model below stays on <optional>/<string>/<string_view>/<vector>, hand-rolls the
// two lookups that would otherwise want <algorithm>, and leaves everything
// path-shaped to :io.
#include <optional>
#include <string>
#include <string_view>
#include <vector>
export module cup.project:config;

export namespace cup::project {

// kMarker is the file whose presence identifies a cup project root.
inline constexpr std::string_view kMarker = "cup.toml";

// Build tools cup can scaffold and drive. CMake is the default; Make targets the
// headers family (C++11/14/17) with discovery-based Makefiles.
inline constexpr std::string_view kToolCMake = "cmake";
inline constexpr std::string_view kToolMake = "make";

// DockerImage is one `[[docker.image]]` entry: a build image's name (also its
// docker/<name>/ directory and docker image name), the base image its Dockerfile
// builds `FROM`, the last-built version used as its tag, and a hash of the
// last-built Dockerfile so `cup docker build` can bump the version only when the
// content actually changed.
struct DockerImage {
    std::string name;
    std::string base;
    int version = 0;
    std::string hash;
    // Spelled is_default because `default` is a keyword; the TOML key stays
    // "default", so cup.toml is unchanged.
    bool is_default = false;

    // Spelled out rather than `= default`: GCC 16 segfaults in
    // module_state::mangle(bool) when serialising a defaulted friend
    // operator== for this struct as part of a module interface.
    //
    // Sonar's "use =default" is deliberately left unsuppressed, so the issue
    // stays open as the reminder to revisit this — and drop the workaround —
    // once the GCC bug is fixed.
    friend bool operator==(const DockerImage& lhs, const DockerImage& rhs) {
        return lhs.name == rhs.name && lhs.base == rhs.base && lhs.version == rhs.version &&
               lhs.hash == rhs.hash && lhs.is_default == rhs.is_default;
    }
};

// DockerConfig is the `[docker]` table in cup.toml: the build images cup manages
// for the project (each `docker/<name>/Dockerfile`) and the registry `cup docker
// push` publishes them to. One image is the auto-updating default build image,
// created by `cup new` and kept in sync with the project's apt dependencies.
struct DockerConfig {
    std::string registry;
    std::vector<DockerImage> images;

    // See DockerImage::operator== for why this is spelled out rather than
    // `= default`.
    friend bool operator==(const DockerConfig& lhs, const DockerConfig& rhs) {
        return lhs.registry == rhs.registry && lhs.images == rhs.images;
    }

    // default_image returns the project's auto-updating default build image, or
    // nullptr when none is configured (projects created before the [docker] table
    // existed). Go returns (*DockerImage, bool); a null pointer carries the same
    // information without the second return.
    [[nodiscard]] const DockerImage* default_image() const { return seek_default(*this); }
    [[nodiscard]] DockerImage* default_image() { return seek_default(*this); }

    // find returns the image with the given name, or nullptr if the project
    // defines no such build image.
    [[nodiscard]] const DockerImage* find(std::string_view name) const {
        return seek_named(*this, name);
    }
    [[nodiscard]] DockerImage* find(std::string_view name) { return seek_named(*this, name); }

    // empty reports whether the table would serialise to nothing, which is when
    // cup.toml leaves `[docker]` out entirely.
    [[nodiscard]] bool empty() const { return registry.empty() && images.empty(); }

private:
    // The two lookups above are each written once here rather than as a const
    // version plus a const_cast off it. Self deduces the caller's constness, so
    // the pointer that comes back is const exactly when the config is — no
    // qualification is ever cast away, and the null case picks up the same
    // pointer type from images.data().
    //
    // Private member *functions* leave the aggregate alone: registry and images
    // are still public non-static data members, so DockerConfig{.registry = ...}
    // keeps working.
    //
    // The return type is spelled as a trailing decltype rather than deduced: the
    // callers above sit earlier in the class, and a deduced `auto*` is not
    // available to them ("use of ... before deduction of 'auto'").
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

// CompilerConfig is the `[compiler]` table in cup.toml: the minimum compiler major
// versions the project's generated CMakeLists enforces, plus the docker image
// `cup compiler` compiles in to verify a version change before committing it. GCC
// and Clang are pinned independently — a project may enforce one, both, or
// neither.
//
// An unpinned compiler is nullopt, not 0, and is left out of cup.toml entirely
// rather than written as a meaningless `gcc = 0`. The distinction is load-bearing:
// cup rewrites cup.toml in place on every `cup compiler set`, so a floor collapsed
// to 0 on the way out would silently unpin the compiler.
struct CompilerConfig {
    std::optional<int> gcc;
    std::optional<int> clang;
    std::string verify_image;

    // See DockerImage::operator== for why this is spelled out rather than
    // `= default`.
    friend bool operator==(const CompilerConfig& lhs, const CompilerConfig& rhs) {
        return lhs.gcc == rhs.gcc && lhs.clang == rhs.clang && lhs.verify_image == rhs.verify_image;
    }

    // gcc_floor and clang_floor return the pinned major version, or 0 when the
    // compiler is unpinned, so callers can work in plain ints (0 = no floor).
    [[nodiscard]] int gcc_floor() const { return gcc.value_or(0); }
    [[nodiscard]] int clang_floor() const { return clang.value_or(0); }

    // has_floor reports whether cup.toml pins any compiler minimum. When it does
    // not (older projects predate the [compiler] table), callers fall back to
    // cup's per-standard defaults. verify_image alone is not a version floor.
    [[nodiscard]] bool has_floor() const { return gcc.has_value() || clang.has_value(); }

    // empty reports whether the table would serialise to nothing.
    [[nodiscard]] bool empty() const { return !has_floor() && verify_image.empty(); }
};

// make_compiler_config builds a [compiler] table from major versions, treating 0
// as "no floor" — that compiler is left unpinned and omitted from cup.toml.
// (Go: NewCompilerConfig.)
[[nodiscard]] CompilerConfig make_compiler_config(int gcc, int clang) {
    const auto floor = [](int v) { return v > 0 ? std::optional<int>(v) : std::nullopt; };
    return CompilerConfig{.gcc = floor(gcc), .clang = floor(clang), .verify_image = {}};
}

// Config is the parsed contents of cup.toml.
//
// std_module overrides how a modules-family project reaches the standard library;
// see uses_std_module. It is optional so "unset" (follow the standard) stays
// distinguishable from an explicit `std_module = false`.
struct Config {
    std::string name;
    std::string cup_version;
    int cpp_standard = 0;
    std::optional<bool> std_module;
    std::string build_tool;
    CompilerConfig compiler;
    DockerConfig docker;

    // See DockerImage::operator== for why this is spelled out rather than
    // `= default`.
    friend bool operator==(const Config& lhs, const Config& rhs) {
        return lhs.name == rhs.name && lhs.cup_version == rhs.cup_version &&
               lhs.cpp_standard == rhs.cpp_standard && lhs.std_module == rhs.std_module &&
               lhs.build_tool == rhs.build_tool && lhs.compiler == rhs.compiler &&
               lhs.docker == rhs.docker;
    }

    // standard returns the project's C++ standard, defaulting to 23 when unset so
    // projects created before cpp_standard existed keep behaving as C++23.
    [[nodiscard]] int standard() const { return cpp_standard == 0 ? 23 : cpp_standard; }

    // uses_std_module reports whether the project reaches the standard library
    // through `import std;` rather than through a global module fragment of
    // #includes. It is the condition behind the std_import / std_prelude split in
    // scaffolded sources.
    //
    // cup.toml's std_module decides when set; otherwise the standard does, since
    // C++23 is the first with a std module. The two are separate capabilities in
    // practice: named modules need GCC 14, while `import std;` additionally needs
    // GCC 15 and CMake 3.30 behind an experimental gate. A project that wants
    // C++23 on a GCC 14 floor — cup's own C++ port under cpp/ — records
    // `std_module = false`, and cup keeps scaffolding global-module-fragment
    // sources at the newer standard.
    [[nodiscard]] bool uses_std_module() const { return std_module.value_or(standard() >= 23); }

    // tool returns the project's build tool, defaulting to CMake when unset so
    // projects created before build_tool existed keep building with CMake.
    [[nodiscard]] std::string_view tool() const {
        return build_tool.empty() ? kToolCMake : std::string_view(build_tool);
    }

    // uses_modules reports whether the project's standard supports C++ modules
    // (C++20 and later); below that, cup scaffolds classic headers.
    [[nodiscard]] bool uses_modules() const { return standard() >= 20; }

    // uses_make reports whether the project builds with Make rather than CMake.
    [[nodiscard]] bool uses_make() const { return tool() == kToolMake; }
};

}  // namespace cup::project
