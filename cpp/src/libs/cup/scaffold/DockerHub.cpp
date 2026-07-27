// Implementation unit for cup.scaffold:dockerhub — the tags a base-image picker
// offers. Port of internal/scaffold/dockerhub.go.
module;
// nlohmann/json in an implementation unit's fragment, where a header this size does
// not reach any BMI — the same call Releases.cpp makes, for the same reason.
#include <nlohmann/json.hpp>

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>
module cup.scaffold;

import cup.platform;

namespace cup::scaffold {
namespace detail {

std::string normalize_repo(std::string_view repo) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    // Trim whitespace, then the slashes around it, so "/library/gcc/" and
    // "  ubuntu  " both arrive as the registry spells them.
    if (const auto first = repo.find_first_not_of(kSpace); first != std::string_view::npos) {
        repo = repo.substr(first, repo.find_last_not_of(kSpace) - first + 1);
    } else {
        repo = {};
    }
    while (repo.starts_with('/')) {
        repo.remove_prefix(1);
    }
    while (repo.ends_with('/')) {
        repo.remove_suffix(1);
    }

    if (repo.find('/') == std::string_view::npos) {
        return std::format("library/{}", repo);
    }
    return std::string(repo);
}

std::vector<std::string> parse_docker_hub_tags(std::string_view body) {
    // Non-throwing parse: a reply cup cannot read is no tags, which leaves the
    // picker with whatever the caller offers as a default rather than failing a
    // `cup new`.
    const auto document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return {};
    }
    const auto results = document.find("results");
    if (results == document.end() || !results->is_array()) {
        return {};
    }

    std::vector<std::string> tags;
    tags.reserve(results->size());
    for (const auto& entry : *results) {
        if (!entry.is_object()) {
            continue;
        }
        // The API's order is the caller's order — it is already newest-first, and
        // nothing here sorts it.
        if (auto name = entry.value("name", std::string{}); !name.empty()) {
            tags.push_back(std::move(name));
        }
    }
    return tags;
}

std::expected<std::vector<std::string>, utils::error::Error> fetch_docker_hub_tags(
    std::string_view repo) {
    // vformat rather than format: the endpoint is a constant held in the interface,
    // so the format string is a value here rather than a literal, and only the
    // runtime form accepts one.
    const std::string repository = normalize_repo(repo);
    const std::string url = std::vformat(kDockerHubTagsUrl, std::make_format_args(repository));
    return platform::http_get(url).transform(
        [](const std::string& body) { return parse_docker_hub_tags(body); });
}

}  // namespace detail

std::expected<std::vector<std::string>, utils::error::Error> docker_hub_tags(
    std::string_view repo) {
    return detail::current_docker_hub_tags()(repo);
}

}  // namespace cup::scaffold
