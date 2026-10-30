module;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// <cstdlib> first: see Process.cpp.
#include <cstdlib>
#include <windows.h>
#include <winhttp.h>

#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>
module windows;

namespace windows {

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

}
