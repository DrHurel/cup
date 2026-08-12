module;
#include <curl/curl.h>

#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
module cup.platform;

namespace cup::platform {
namespace {

// 8 MiB, matching Go's io.LimitReader(resp.Body, 8<<20) cap.
constexpr std::size_t kMaxBody = 8 << 20;

std::size_t write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    const std::size_t added = size * nmemb;
    if (body->size() + added > kMaxBody) {
        return 0; // a short write tells curl to abort with CURLE_WRITE_ERROR
    }
    body->append(ptr, added);
    return added;
}

struct SList {
    curl_slist* list = nullptr;
    ~SList() { curl_slist_free_all(list); }
};

}

std::expected<std::string, error::Error> http_get(std::string_view url) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return std::unexpected(error::Error("curl_easy_init failed"));
    }

    std::string body;
    char error_buf[CURL_ERROR_SIZE] = {};
    const std::string url_str(url);

    SList headers;
    headers.list = curl_slist_append(headers.list, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url_str.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cup");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&body));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // Every caller passes a fixed https:// constant (GNU/GitHub/Docker Hub) or,
    // in tests, a local http:// server — but http_get's own signature accepts
    // any url, so nothing here proves that statically. Restricting both the
    // initial request and any redirect to http/https (mirrored to
    // CURLOPT_REDIR_PROTOCOLS by default) closes the classic curl SSRF vector:
    // a malicious redirect hop to file://, scp://, gopher:// etc.
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");

    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return std::unexpected(error::Error(std::format("GET {}: {}", url, error_buf)));
    }
    if (status != 200) {
        return std::unexpected(error::Error(std::format("GET {}: HTTP {}", url, status)));
    }
    return body;
}

}
