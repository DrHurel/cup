module;
#include <expected>
#include <string>
#include <string_view>
#include <vector>
module cup.scaffold;

import cup.platform;

namespace cup::scaffold {
namespace {

std::string build_tags_url(std::string_view repo) {
    const std::string normalized = detail::normalize_repo(repo);
    std::string url = docker_hub_tags_url_template();
    if (const auto placeholder = url.find("%s"); placeholder != std::string::npos) {
        url.replace(placeholder, 2, normalized);
    }
    return url;
}

}

std::expected<Tags, error::Error> fetch_docker_hub_tags(std::string_view repo) {
    return platform::http_get(build_tags_url(repo))
        .transform([](const std::string& body) { return Tags{detail::parse_docker_hub_tags(body)}; });
}

}
