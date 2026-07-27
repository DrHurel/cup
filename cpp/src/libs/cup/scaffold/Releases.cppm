module;
// Declarations only; the definitions are in Releases.cpp, which is where nlohmann/json,
// <regex>, <chrono> and <filesystem> stay. See the note at the top of scaffold.cppm.
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
export module cup.scaffold:releases;

import :compiler;
// Re-exported: utils::error::Error is the E of write_release_cache's result.
export import utils.error;

export namespace cup::scaffold {

// newest_compilers returns the newest released GCC and Clang major versions, used
// as the ceiling of the `cup new` floor picker.
//
// It never fails: an unreachable network, a rate-limited endpoint or a changed
// upstream format all fall back — to the on-disk cache, then to the constants
// below — so the picker narrows rather than breaking. That is what makes shipping
// without libcurl a supported degradation.
[[nodiscard]] Compilers newest_compilers();

// NewestCompilersFunc is the source newest_compilers reads from. A function pointer
// rather than a std::function, for the reason cup.platform's HttpGet gives.
// (Go: NewestCompilersFunc.)
using NewestCompilersFunc = Compilers (*)();

namespace detail {

// The newest majors cup assumes when the live release lists cannot be reached. The
// baseline (min_compilers) always wins as the floor, so a stale fallback only ever
// narrows the top of the picker, never breaks it.
inline constexpr int kGccNewestFallback = 15;
inline constexpr int kClangNewestFallback = 20;

// kReleaseCacheTtl bounds how long a fetched release list is trusted before cup
// looks again. Compiler majors ship a few times a year at most, so a week keeps
// `cup new` off the network on all but the occasional run.
inline constexpr std::int64_t kReleaseCacheTtlSeconds = 7 * 24 * 60 * 60;

// The live sources of the two release lists.
inline constexpr std::string_view kGccReleasesUrl = "https://ftp.gnu.org/gnu/gcc/";
inline constexpr std::string_view kClangReleasesUrl =
    "https://api.github.com/repos/llvm/llvm-project/releases?per_page=10";

// ReleaseCache is what cup remembers between runs, at
// <user cache dir>/cup/compiler-releases.json.
//
// fetched_at is Unix seconds here and RFC 3339 on disk, because the file is shared
// with the Go implementation for as long as both binaries exist — a Go-written
// cache has to stay readable, and one written here has to stay readable by Go.
// (Keeping <chrono> out of this interface is the other half of the reason; see the
// header note.)
struct ReleaseCache {
    int gcc = 0;
    int clang = 0;
    std::int64_t fetched_at = 0;
};

// fetch_newest_compilers resolves the ceiling from, in order of preference: a fresh
// on-disk cache, a live fetch (cached on success), the last cached value, and
// finally the fallback constants above.
[[nodiscard]] Compilers fetch_newest_compilers();

// fetch_gcc_newest and fetch_clang_newest read one release list each, returning 0
// when it cannot be determined.
[[nodiscard]] int fetch_gcc_newest();
[[nodiscard]] int fetch_clang_newest();

// parse_gcc_newest returns the largest gcc major named in a directory listing like
// the GNU FTP index (entries such as "gcc-15.1.0/").
[[nodiscard]] int parse_gcc_newest(std::string_view body);

// parse_clang_newest returns the largest major among non-prerelease LLVM releases,
// whose tags look like "llvmorg-20.1.8".
[[nodiscard]] int parse_clang_newest(std::string_view body);

// release_cache_path is where the cache lives, or nullopt when the environment
// names no cache directory. (Go: os.UserCacheDir + "cup/compiler-releases.json".)
//
// A std::string rather than a std::filesystem::path, and that is the constraint
// talking rather than a preference: this partition was the fifth to carry
// <filesystem>, and with it the primary could no longer read its BMI ("failed to
// read compiled module cluster N: Bad file data"). It is the only path-shaped value
// in :releases, so it is the one that gives way — see point 2 at the top of
// scaffold.cppm.
[[nodiscard]] std::optional<std::string> release_cache_path();

// read_release_cache reads the cache, reporting nullopt for "no usable cache" —
// missing, unreadable and malformed are one case, because each means the same
// thing to the caller.
[[nodiscard]] std::optional<ReleaseCache> read_release_cache();

// write_release_cache stores the cache, creating its directory.
[[nodiscard]] std::expected<void, utils::error::Error> write_release_cache(
    const ReleaseCache& cache);

// now_unix is the current time in Unix seconds — the clock the TTL is measured on,
// exported so a test can seed a cache with a controlled age.
[[nodiscard]] std::int64_t now_unix();

// first_non_zero returns a unless it is 0. It is what makes the fallback chain read
// as a chain. (Go: firstNonZero.)
[[nodiscard]] int first_non_zero(int a, int b);

// current_newest_compilers holds the installed source — fetch_newest_compilers
// unless a test replaced it.
inline NewestCompilersFunc& current_newest_compilers() {
    static NewestCompilersFunc source = detail::fetch_newest_compilers;
    return source;
}

}  // namespace detail

// ScopedNewestCompilers installs a release source for the lifetime of the guard and
// restores the previous one after — the seam that keeps the suites off the network
// and off the clock. (Go: assigning NewestCompilersFunc with a t.Cleanup.)
class ScopedNewestCompilers {
public:
    explicit ScopedNewestCompilers(NewestCompilersFunc source) {
        detail::current_newest_compilers() = source;
    }
    ScopedNewestCompilers(const ScopedNewestCompilers&) = delete;
    ScopedNewestCompilers& operator=(const ScopedNewestCompilers&) = delete;
    ~ScopedNewestCompilers() { detail::current_newest_compilers() = previous_; }

private:
    // Captured by the default member initializer, which runs before the constructor
    // body — so previous_ holds the source installed on the way in.
    NewestCompilersFunc previous_ = detail::current_newest_compilers();
};

}  // namespace cup::scaffold
