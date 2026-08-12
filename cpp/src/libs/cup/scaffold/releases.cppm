module;
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
export module cup.scaffold:releases;

export import cup.error;

export namespace cup::scaffold {

// kGccNewestFallback and kClangNewestFallback are the newest majors cup
// assumes when the live release lists cannot be reached (offline,
// rate-limited, or a changed upstream format). The baseline (min_compilers)
// always wins as the floor, so a stale fallback only ever narrows the top of
// the picker, never breaks it.
inline constexpr int kGccNewestFallback = 15;
inline constexpr int kClangNewestFallback = 20;

namespace detail {

// first_non_zero returns a, or b if a is zero.
[[nodiscard]] constexpr int first_non_zero(int a, int b) { return a != 0 ? a : b; }

// parse_gcc_newest returns the largest gcc major named in a directory listing
// like the GNU FTP index (entries such as "gcc-15.1.0/"), or 0 if none is
// found. A hand-rolled scan for "gcc-<digits>.<digit>" rather than <regex>:
// see render.cppm's note on cup.scaffold's one-heavy-partition budget, already
// spent — this partition stays off it entirely, including in its
// implementation unit, since nothing here needs more than a linear scan.
[[nodiscard]] constexpr int parse_gcc_newest(std::string_view body) {
    constexpr std::string_view kPrefix = "gcc-";
    int newest = 0;
    std::size_t pos = 0;
    while (true) {
        pos = body.find(kPrefix, pos);
        if (pos == std::string_view::npos) {
            break;
        }
        std::size_t i = pos + kPrefix.size();
        const std::size_t major_start = i;
        while (i < body.size() && body[i] >= '0' && body[i] <= '9') {
            ++i;
        }
        const bool has_major = i > major_start;
        if (has_major && i < body.size() && body[i] == '.' && i + 1 < body.size() &&
            body[i + 1] >= '0' && body[i + 1] <= '9') {
            int major = 0;
            for (std::size_t k = major_start; k < i; ++k) {
                major = major * 10 + (body[k] - '0');
            }
            if (major > newest) {
                newest = major;
            }
        }
        pos = has_major ? i : pos + kPrefix.size();
    }
    return newest;
}

// parse_clang_newest returns the largest major among non-prerelease LLVM
// releases, whose tags look like "llvmorg-20.1.8", in a GitHub releases JSON
// array ([{"tag_name":"...","prerelease":bool}, ...]). A hand-rolled scan of
// just these two fields rather than a general JSON parser — cup only ever
// reads its own narrow shape of this response.
[[nodiscard]] int parse_clang_newest(std::string_view body) {
    constexpr std::string_view kTagKey = "\"tag_name\"";
    constexpr std::string_view kPrereleaseTrue = "\"prerelease\":true";
    constexpr std::string_view kTagPrefix = "llvmorg-";
    int newest = 0;
    std::size_t pos = 0;
    while (true) {
        const auto obj_start = body.find('{', pos);
        if (obj_start == std::string_view::npos) {
            break;
        }
        const auto obj_end = body.find('}', obj_start);
        if (obj_end == std::string_view::npos) {
            break;
        }
        const std::string_view object = body.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;

        if (object.find(kPrereleaseTrue) != std::string_view::npos) {
            continue;
        }
        const auto tag_key = object.find(kTagKey);
        if (tag_key == std::string_view::npos) {
            continue;
        }
        const auto value_start = object.find('"', object.find(':', tag_key) + 1);
        if (value_start == std::string_view::npos) {
            continue;
        }
        const auto value_end = object.find('"', value_start + 1);
        if (value_end == std::string_view::npos) {
            continue;
        }
        const std::string_view tag = object.substr(value_start + 1, value_end - value_start - 1);
        if (!tag.starts_with(kTagPrefix)) {
            continue;
        }
        std::string_view rest = tag.substr(kTagPrefix.size());
        const auto dot = rest.find('.');
        if (dot == std::string_view::npos) {
            continue;
        }
        rest = rest.substr(0, dot);
        if (rest.empty()) {
            continue;
        }
        int major = 0;
        bool all_digits = true;
        for (const char c : rest) {
            if (c < '0' || c > '9') {
                all_digits = false;
                break;
            }
            major = major * 10 + (c - '0');
        }
        if (all_digits && major > newest) {
            newest = major;
        }
    }
    return newest;
}

}

// gcc_releases_url / clang_releases_url point at the live release indexes;
// overridable in tests so they never touch the network.
[[nodiscard]] std::string& gcc_releases_url() {
    static std::string url = "https://ftp.gnu.org/gnu/gcc/";
    return url;
}
[[nodiscard]] std::string& clang_releases_url() {
    static std::string url = "https://api.github.com/repos/llvm/llvm-project/releases?per_page=10";
    return url;
}

// A release-ceiling cache entry, mirroring the on-disk cache: fetched_at is
// plain epoch seconds rather than a timestamp type, keeping this interface
// partition off <chrono> (and so off the heavy-header budget — see
// render.cppm's note).
struct ReleaseCache {
    int gcc = 0;
    int clang = 0;
    long long fetched_at_epoch_seconds = 0;
};

// release_cache_path, read_release_cache and write_release_cache manage the
// on-disk ceiling cache (~/.cache/cup/compiler-releases.json, honouring
// XDG_CACHE_HOME). Declared here, defined in Releases.cpp: <filesystem> and
// timestamp handling stay out of this interface partition for the same
// reason as :render / :cmake's split.
[[nodiscard]] std::string release_cache_path();
[[nodiscard]] std::optional<ReleaseCache> read_release_cache();
[[nodiscard]] std::expected<void, error::Error> write_release_cache(const ReleaseCache& cache);

// fetch_gcc_newest / fetch_clang_newest / fetch_newest_compilers are declared
// here, defined in Releases.cpp: they call cup.platform::http_get (a
// cross-module import) and touch the on-disk cache, both of which this
// interface partition stays clear of.
[[nodiscard]] int fetch_gcc_newest();
[[nodiscard]] int fetch_clang_newest();
[[nodiscard]] std::pair<int, int> fetch_newest_compilers();

// newest_compilers_func is the source of the picker's ceiling; overridable in
// tests to return fixed versions without a fetch.
using NewestCompilersFunc = std::pair<int, int> (*)();
[[nodiscard]] NewestCompilersFunc& newest_compilers_func() {
    static NewestCompilersFunc f = &fetch_newest_compilers;
    return f;
}

// newest_compilers returns the newest released GCC and Clang major versions,
// used as the ceiling of the `cup new` floor picker.
[[nodiscard]] std::pair<int, int> newest_compilers() { return newest_compilers_func()(); }

}
