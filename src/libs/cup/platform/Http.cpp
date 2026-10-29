module;
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// <cstdlib> first, outside any extern "C": <windows.h> reaches it through
// the intrinsics headers from inside an extern "C" block, and GCC's modules
// then reject those declarations as conflicting with the C++-linkage ones
// cup.error's BMI already carries.
#include <cstdlib>
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>
module cup.platform;

namespace cup::platform {

#ifdef _WIN32

// WinHTTP, not libcurl, on Windows: it ships with every Windows install and
// uses the OS certificate store and TLS stack (SChannel), so a Windows build
// of cup needs no curl/OpenSSL to link or CA bundle to ship — it can be one
// statically linked cup.exe. The behaviour mirrors the libcurl branch below:
// same User-Agent and Accept header, 4 s timeout, http/https only, TLS 1.2
// floor, redirects followed, and the same 8 MiB body cap.
namespace {

// 8 MiB, matching Go's io.LimitReader(resp.Body, 8<<20) cap.
constexpr std::size_t kMaxBody = 8 << 20;
constexpr int kTimeoutMs = 4000;

// Closes a WinHTTP handle on scope exit (session, connection and request
// each need their own WinHttpCloseHandle).
struct InternetHandle {
    HINTERNET h = nullptr;
    explicit InternetHandle(HINTERNET handle) : h(handle) {}
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    ~InternetHandle() {
        if (h != nullptr) {
            ::WinHttpCloseHandle(h);
        }
    }
};

std::wstring widen_url(std::string_view s) {
    if (s.empty()) {
        return {};
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

error::Error get_error(std::string_view url, std::string_view step) {
    return error::Error(std::format("GET {}: {} failed (error {})", url, step,
                                    static_cast<unsigned long>(::GetLastError())));
}

}

std::expected<std::string, error::Error> http_get(std::string_view url) {
    const std::wstring wurl = widen_url(url);

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wurl.empty() ||
        ::WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &parts) == 0) {
        return std::unexpected(error::Error(std::format("GET {}: malformed URL", url)));
    }
    // The same http/https-only restriction the libcurl branch sets with
    // CURLOPT_PROTOCOLS_STR; WinHTTP itself only speaks those two, but say so
    // plainly rather than relying on that.
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::unexpected(error::Error(std::format("GET {}: unsupported scheme", url)));
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) {
        path = L"/";
    }

    InternetHandle session(::WinHttpOpen(L"cup", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.h == nullptr) {
        return std::unexpected(get_error(url, "WinHttpOpen"));
    }
    ::WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    // Floors the handshake at TLS 1.2, like CURL_SSLVERSION_TLSv1_2.
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    ::WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    InternetHandle connection(::WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (connection.h == nullptr) {
        return std::unexpected(get_error(url, "WinHttpConnect"));
    }
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(::WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                flags));
    if (request.h == nullptr) {
        return std::unexpected(get_error(url, "WinHttpOpenRequest"));
    }
    // WinHTTP follows redirects by default, but never from https down to
    // http (WINHTTP_OPTION_REDIRECT_POLICY's default) — stricter than curl's
    // CURLOPT_FOLLOWLOCATION, and the right call for a TLS-pinned fetch.
    constexpr wchar_t kAccept[] = L"Accept: application/vnd.github+json";
    if (::WinHttpSendRequest(request.h, kAccept, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA,
                             0, 0, 0) == 0 ||
        ::WinHttpReceiveResponse(request.h, nullptr) == 0) {
        return std::unexpected(get_error(url, "request"));
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (::WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                              WINHTTP_NO_HEADER_INDEX) == 0) {
        return std::unexpected(get_error(url, "reading the status"));
    }

    std::string body;
    std::vector<char> chunk;
    while (true) {
        DWORD available = 0;
        if (::WinHttpQueryDataAvailable(request.h, &available) == 0) {
            return std::unexpected(get_error(url, "reading the body"));
        }
        if (available == 0) {
            break;
        }
        if (body.size() + available > kMaxBody) {
            return std::unexpected(
                error::Error(std::format("GET {}: response larger than {} bytes", url, kMaxBody)));
        }
        chunk.resize(available);
        DWORD read = 0;
        if (::WinHttpReadData(request.h, chunk.data(), available, &read) == 0) {
            return std::unexpected(get_error(url, "reading the body"));
        }
        body.append(chunk.data(), read);
    }

    if (status != 200) {
        return std::unexpected(error::Error(std::format("GET {}: HTTP {}", url, status)));
    }
    return body;
}

#else
namespace {

// 8 MiB, matching Go's io.LimitReader(resp.Body, 8<<20) cap.
constexpr std::size_t kMaxBody = 8 << 20;

// Signature fixed by libcurl's own curl_write_callback typedef (char* buffer,
// not const char*; void* userdata, not a typed pointer) — CURLOPT_WRITEFUNCTION
// calls back through that exact C function pointer type, so a "more
// meaningful" signature here would be undefined behaviour, not a style choice.
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
    // A copy would free the same list twice; only one owner is ever needed.
    SList() = default;
    SList(const SList&) = delete;
    SList& operator=(const SList&) = delete;
};

}

std::expected<std::string, error::Error> http_get(std::string_view url) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return std::unexpected(error::Error("curl_easy_init failed"));
    }

    std::string body;
    std::array<char, CURL_ERROR_SIZE> error_buf{};
    const std::string url_str(url);

    SList headers;
    headers.list = curl_slist_append(headers.list, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url_str.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cup");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&body));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf.data());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // Every caller passes a fixed https:// constant (GNU/GitHub/Docker Hub) or,
    // in tests, a local http:// server — but http_get's own signature accepts
    // any url, so nothing here proves that statically. Restricting both the
    // initial request and any redirect to http/https (mirrored to
    // CURLOPT_REDIR_PROTOCOLS by default) closes the classic curl SSRF vector:
    // a malicious redirect hop to file://, scp://, gopher:// etc.
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    // Floors the handshake at TLS 1.2 rather than trusting curl's build-time
    // default, which can be as low as TLS 1.0 depending on how the platform's
    // libcurl/OpenSSL were configured.
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return std::unexpected(error::Error(std::format("GET {}: {}", url, error_buf.data())));
    }
    if (status != 200) {
        return std::unexpected(error::Error(std::format("GET {}: HTTP {}", url, status)));
    }
    return body;
}

#endif

}
