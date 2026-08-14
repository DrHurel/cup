module;
#include <expected>
#include <string>
#include <string_view>
export module cup.platform:net;

export import cup.error;

export namespace cup::platform {

// http_get fetches url with cup's timeout and a User-Agent (GitHub rejects
// requests without one), capping the body so a surprising response can't
// exhaust memory. Defined in Http.cpp: libcurl lives in a module
// implementation unit, not here, for the same reason toml++ lives in
// cup.project/Toml.cpp rather than its :io interface partition — see
// docs/migration-cpp23.md's constraint 2.
[[nodiscard]] std::expected<std::string, error::Error> http_get(std::string_view url);

}
