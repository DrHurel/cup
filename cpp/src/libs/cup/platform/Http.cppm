module;
// Declarations only — libcurl is included in Http.cpp, a module implementation
// unit whose global module fragment never reaches a BMI. That is the rule Phase 2
// paid for with an ICE (see the note at the top of cup.project's io.cppm), applied
// up front this time: a third-party header in an *interface* unit's fragment is
// what makes GCC 14 segfault while the primary merges the partition.
//
// The headers left here are the light ones the other partitions already repeat.
#include <expected>
#include <string>
#include <string_view>
export module cup.platform:http;

// Re-exported: cup::error::Error is the E of every result below.
export import cup.error;

export namespace cup::platform {

// HttpGet is the shape of a fetch: a URL in, the response body or an error out.
//
// A plain function pointer rather than a std::function, and that is the same GCC
// 14 constraint the note on http_get below spells out: instantiating
// std::function<std::expected<std::string, Error>(...)> in an interface unit
// breaks every implementation unit of the module that uses std::format. A
// function pointer holds no such machinery. The cost is that a substituted
// fetcher must be captureless — the suites keep what they want to observe in a
// file-scope variable, which is what the Go tests do with their package-level
// stubs anyway.
using HttpGet = std::expected<std::string, error::Error> (*)(std::string_view);

namespace detail {

// curl_get is the real transport, defined in Http.cpp. It is the only thing in cup
// that speaks HTTP, which is what keeps libcurl behind this seam — and what makes
// "ship without libcurl" a supported degradation rather than a rewrite.
[[nodiscard]] std::expected<std::string, error::Error> curl_get(std::string_view url);

// current_http_get holds the installed fetcher — curl_get unless a test replaced
// it. The one piece of mutable state in cup.platform, and the same shape as
// cup.ui's colour_enabled().
inline HttpGet& current_http_get() {
    static HttpGet fetch = detail::curl_get;
    return fetch;
}

}  // namespace detail

// http_get fetches url and returns the response body.
//
// cup uses it to discover compiler releases and Docker Hub tags, and every caller
// treats a failure as "narrow the picker", never as a fatal error — see the
// fallbacks in cup.scaffold:releases. (Go: scaffold.httpGet.)
//
// It is a one-line forward to current_http_get(), and it is still declared here
// and defined in Http.cpp rather than written inline. That is not a style choice:
// an *inline* function defined in an interface unit and returning
// std::expected<std::string, Error> makes GCC 14 miscompile every implementation
// unit of the same module that instantiates std::format —
//
//     error: satisfaction of atomic constraint
//            'requires{...std::expected<_Tp, _Er>::swap...}' depends on itself
//
// pointed at the std::format call, not at anything nearby. Computing whether the
// expected is swappable needs std::string's own swappability, and the module BMI
// feeds that check back into itself. The same body returning
// std::expected<int, Error> or std::expected<void, Error> is fine, so it is the
// non-trivial value type that does it. Declaring here and defining there costs
// nothing and is the rule cup.scaffold's partitions follow throughout.
[[nodiscard]] std::expected<std::string, error::Error> http_get(std::string_view url);

// ScopedHttpGet installs a fetcher for the lifetime of the guard and restores the
// previous one after — the test seam that keeps the suites off the network.
//
// Go stubs one level up instead (NewestCompilersFunc, DockerHubTagsFunc) because a
// Go test can stand up a real http.Server for the level below; doing that from a
// C++ suite would mean writing an HTTP server to test an HTTP client. Substituting
// the fetcher tests the same seam and lets the suite assert the *URL* that was
// built, which the Go tests reach for indirectly through the request path.
// (Go: ui.SetInput's restore func; C++: cup::ui::ScopedInput.)
class ScopedHttpGet {
public:
    explicit ScopedHttpGet(HttpGet fetch) { detail::current_http_get() = fetch; }
    ScopedHttpGet(const ScopedHttpGet&) = delete;
    ScopedHttpGet& operator=(const ScopedHttpGet&) = delete;
    ~ScopedHttpGet() { detail::current_http_get() = previous_; }

private:
    // Captured by the default member initializer, which runs before the constructor
    // body — so previous_ holds the fetcher installed on the way in, not the one
    // the body swaps to. (Same trick as cup::ui::ScopedInput.)
    HttpGet previous_ = detail::current_http_get();
};

}  // namespace cup::platform
