module;
#include <expected>
#include <string>
#include <string_view>
#include <vector>
export module cup.scaffold:dockerhub;

export import cup.error;

export namespace cup::scaffold {

namespace detail {

// normalize_repo maps a bare repository name onto Docker Hub's "library"
// namespace (where official images live) and leaves an already-namespaced
// repo untouched.
[[nodiscard]] std::string normalize_repo(std::string_view repo) {
    constexpr std::string_view kSpace = " \t\n\r\f\v";
    auto first = repo.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        repo = {};
    } else {
        const auto last = repo.find_last_not_of(kSpace);
        repo = repo.substr(first, last - first + 1);
    }
    while (repo.starts_with('/')) {
        repo.remove_prefix(1);
    }
    while (repo.ends_with('/')) {
        repo.remove_suffix(1);
    }
    if (repo.find('/') == std::string_view::npos) {
        return "library/" + std::string(repo);
    }
    return std::string(repo);
}

// parse_docker_hub_tags pulls the tag names out of a Docker Hub tags response
// ({"results":[{"name":"14"},…]}), preserving the API's newest-first order and
// dropping blanks. A hand-rolled scan of the one field cup reads, not a
// general JSON parser — same reasoning as releases.cppm's parse_clang_newest.
[[nodiscard]] std::vector<std::string> parse_docker_hub_tags(std::string_view body) {
    constexpr std::string_view kNameKey = "\"name\":\"";
    std::vector<std::string> tags;
    std::size_t pos = 0;
    while (true) {
        const auto key = body.find(kNameKey, pos);
        if (key == std::string_view::npos) {
            break;
        }
        const auto value_start = key + kNameKey.size();
        const auto value_end = body.find('"', value_start);
        if (value_end == std::string_view::npos) {
            break;
        }
        const std::string_view name = body.substr(value_start, value_end - value_start);
        if (!name.empty()) {
            tags.emplace_back(name);
        }
        pos = value_end + 1;
    }
    return tags;
}

}

// docker_hub_tags_url_template is the Docker Hub registry endpoint listing a
// repository's tags, newest first, with a "%s" placeholder for the (already
// namespace-normalized) repo. Overridable in tests so they never touch the
// network.
[[nodiscard]] std::string& docker_hub_tags_url_template() {
    static std::string url =
        "https://hub.docker.com/v2/repositories/%s/tags/?page_size=100&ordering=last_updated";
    return url;
}

// Tags wraps the list rather than returning std::vector<std::string> directly:
// std::expected<std::vector<std::string>, Error>::swap trips a GCC 14
// libstdc++ bug (its noexcept-check recurses into <format>'s std::span-from-
// std::string constraint whenever <format> is also reachable in the module —
// true here, since :render/:cmake/:compiler/:releases all use it). A named
// wrapper's swap falls back to move-construct/move-assign instead of probing
// the member's ADL swap, which sidesteps the recursion. Fixed in GCC 15; keep
// the wrapper until the GCC 14 floor is dropped.
struct Tags {
    std::vector<std::string> values;
};

// fetch_docker_hub_tags is declared here, defined in Dockerhub.cpp: it calls
// cup.platform::http_get (a cross-module import), which this interface
// partition stays clear of — see releases.cppm's note.
[[nodiscard]] std::expected<Tags, error::Error> fetch_docker_hub_tags(std::string_view repo);

// docker_hub_tags_func is the source of a repository's tags; overridable in
// tests to return a fixed list without a fetch.
using DockerHubTagsFunc = std::expected<Tags, error::Error> (*)(std::string_view);
[[nodiscard]] DockerHubTagsFunc& docker_hub_tags_func() {
    static DockerHubTagsFunc f = &fetch_docker_hub_tags;
    return f;
}

// docker_hub_tags returns the tags of a Docker Hub repository, newest first,
// so `cup new` / `cup docker new` can offer a version to pick. A bare repo
// like "gcc" is resolved to the official "library/gcc"; a namespaced
// "org/repo" is used as-is.
[[nodiscard]] std::expected<Tags, error::Error> docker_hub_tags(std::string_view repo) {
    return docker_hub_tags_func()(repo);
}

}
