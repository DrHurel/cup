module;
// Declarations only; the definitions are in DockerHub.cpp, which is where
// nlohmann/json stays. See the note at the top of scaffold.cppm.
#include <expected>
#include <string>
#include <string_view>
#include <vector>
export module cup.scaffold:dockerhub;

// Re-exported: utils::error::Error is the E of every result below.
export import utils.error;

export namespace cup::scaffold {

// DockerHubTagsFunc is the source docker_hub_tags reads from. A function pointer
// rather than a std::function, for the reason cup.platform's HttpGet gives — a
// std::function over a signature returning std::expected<std::vector<...>, Error>
// is one of the shapes GCC 14 cannot carry through a module interface.
// (Go: DockerHubTagsFunc.)
using DockerHubTagsFunc =
    std::expected<std::vector<std::string>, utils::error::Error> (*)(std::string_view);

// docker_hub_tags returns the tags of a Docker Hub repository, newest first, so
// `cup new` and `cup docker new` can offer a version to pick. A bare repo like
// "gcc" is resolved to the official "library/gcc"; a namespaced "org/repo" is used
// as-is.
[[nodiscard]] std::expected<std::vector<std::string>, utils::error::Error> docker_hub_tags(
    std::string_view repo);

namespace detail {

// kDockerHubTagsUrl is the registry endpoint listing a repository's tags, newest
// first. The {} is the normalised repository name.
inline constexpr std::string_view kDockerHubTagsUrl =
    "https://hub.docker.com/v2/repositories/{}/tags/?page_size=100&ordering=last_updated";

// normalize_repo maps a bare repository name onto Docker Hub's "library" namespace
// (where the official images live) and leaves an already-namespaced repo untouched.
[[nodiscard]] std::string normalize_repo(std::string_view repo);

// parse_docker_hub_tags pulls the tag names out of a tags response
// ({"results":[{"name":"14"},…]}), preserving the API's newest-first order and
// dropping blanks. A reply that cannot be parsed yields no tags, not an error.
[[nodiscard]] std::vector<std::string> parse_docker_hub_tags(std::string_view body);

// fetch_docker_hub_tags is the real lookup: one request, then the parse above.
[[nodiscard]] std::expected<std::vector<std::string>, utils::error::Error> fetch_docker_hub_tags(
    std::string_view repo);

// current_docker_hub_tags holds the installed source — fetch_docker_hub_tags unless
// a test replaced it.
inline DockerHubTagsFunc& current_docker_hub_tags() {
    static DockerHubTagsFunc source = detail::fetch_docker_hub_tags;
    return source;
}

}  // namespace detail

// ScopedDockerHubTags installs a tag source for the lifetime of the guard and
// restores the previous one after — the seam that keeps the suites off the network.
class ScopedDockerHubTags {
public:
    explicit ScopedDockerHubTags(DockerHubTagsFunc source) {
        detail::current_docker_hub_tags() = source;
    }
    ScopedDockerHubTags(const ScopedDockerHubTags&) = delete;
    ScopedDockerHubTags& operator=(const ScopedDockerHubTags&) = delete;
    ~ScopedDockerHubTags() { detail::current_docker_hub_tags() = previous_; }

private:
    // Captured by the default member initializer, which runs before the constructor
    // body — so previous_ holds the source installed on the way in.
    DockerHubTagsFunc previous_ = detail::current_docker_hub_tags();
};

}  // namespace cup::scaffold
