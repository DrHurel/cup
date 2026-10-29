module;
#include <expected>
#include <string>
#include <string_view>
export module cup.platform:net;

export import cup.error;
import :services;

export namespace cup::platform {

// http_get fetches url with cup's timeout and a User-Agent (GitHub rejects
// requests without one), capping the body so a surprising response can't
// exhaust memory. The HTTP client itself is the injected backend's (libcurl
// in cup.platform.posix, WinHTTP in cup.platform.windows) — see :services.
[[nodiscard]] std::expected<std::string, error::Error> http_get(std::string_view url) {
    return services().http_get(url);
}

}
