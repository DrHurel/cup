// Implementation unit for cup.platform:http — the one place in cup that speaks
// HTTP.
//
// This is a module implementation unit (`module cup.platform;`, no `export`), not
// an interface unit, and that is deliberate: a module implementation unit's global
// module fragment never becomes part of any BMI, so <curl/curl.h> is compiled here
// once and seen by nothing else. Putting a header this size in an interface unit's
// fragment is what made GCC 14 ICE on toml++ in Phase 2 — see the note at the top
// of cup.project's io.cppm.
module;
#include <curl/curl.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
module cup.platform;

namespace cup::platform::detail {
namespace {

// kBodyLimit caps a response so a surprising reply cannot exhaust memory. Go
// wraps the body in an io.LimitReader, which *truncates* rather than failing, so
// the writer below does the same.
constexpr std::size_t kBodyLimit = 8U << 20U;

// kTimeout is cup's whole-request budget. Release discovery is a nicety — it only
// widens a picker — so a slow endpoint must not hold up `cup new`.
constexpr long kTimeoutMs = 4000;

// kUserAgent is required, not decoration: GitHub rejects requests without one.
constexpr const char* kUserAgent = "cup";

// ensure_global_init runs curl_global_init exactly once. libcurl calls it
// implicitly from curl_easy_init, but that path is not thread-safe — and
// cup.scaffold:releases fetches the GCC and Clang lists concurrently.
void ensure_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { ::curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// append_body is libcurl's write callback. It appends up to the cap and always
// reports the full chunk as consumed, so an oversized response is truncated rather
// than aborted — matching io.LimitReader.
std::size_t append_body(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    const std::size_t chunk = size * nmemb;
    if (body->size() < kBodyLimit) {
        body->append(data, std::min(chunk, kBodyLimit - body->size()));
    }
    return chunk;
}

// Handle closes its easy handle however curl_get returns.
class Handle {
public:
    Handle() : handle_(::curl_easy_init()) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (handle_ != nullptr) {
            ::curl_easy_cleanup(handle_);
        }
        if (headers_ != nullptr) {
            ::curl_slist_free_all(headers_);
        }
    }

    [[nodiscard]] CURL* get() const { return handle_; }

    // add_header appends one request header, kept alive until this handle dies.
    void add_header(const char* header) { headers_ = ::curl_slist_append(headers_, header); }
    [[nodiscard]] curl_slist* headers() const { return headers_; }

private:
    CURL* handle_ = nullptr;
    curl_slist* headers_ = nullptr;
};

}  // namespace

std::expected<std::string, error::Error> curl_get(std::string_view url) {
    ensure_global_init();

    Handle handle;
    if (handle.get() == nullptr) {
        return std::unexpected(error::Error("cannot initialise libcurl"));
    }

    // curl needs a NUL-terminated URL, and it does not copy the string unless
    // asked, so the std::string has to outlive the transfer.
    const std::string target(url);
    std::string body;
    handle.add_header("Accept: application/vnd.github+json");

    ::curl_easy_setopt(handle.get(), CURLOPT_URL, target.c_str());
    ::curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, handle.headers());
    ::curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, kUserAgent);
    ::curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, kTimeoutMs);
    // Go's http.Client follows redirects by default (up to 10 hops).
    ::curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
    ::curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &append_body);
    ::curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &body);
    // Required because the two release fetches run on separate threads: without
    // it libcurl implements its DNS timeout with SIGALRM, which is process-wide
    // and not safe to raise from a worker.
    ::curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);

    if (const CURLcode code = ::curl_easy_perform(handle.get()); code != CURLE_OK) {
        const std::string_view reason = ::curl_easy_strerror(code);
        return std::unexpected(error::Error(std::format("GET {}: {}", url, reason)));
    }

    long status = 0;
    ::curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    // A zero status means the scheme carries none — file:// is the case that
    // matters, since it is how the suite exercises this function without a
    // network. Every HTTP reply sets one, so this cannot mask a real failure.
    if (status != 0 && status != 200) {
        return std::unexpected(error::Error(std::format("GET {}: {}", url, status)));
    }
    return body;
}

}  // namespace cup::platform::detail

namespace cup::platform {

// Defined here rather than inline in Http.cppm — see the note on its declaration
// for the GCC 14 bug that forces the split.
std::expected<std::string, error::Error> http_get(std::string_view url) {
    return detail::current_http_get()(url);
}

}  // namespace cup::platform
