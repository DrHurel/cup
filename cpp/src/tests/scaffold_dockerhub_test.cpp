// Port of internal/scaffold/dockerhub_test.go.
//
// The Go tests already pass against the Go implementation, so they define the
// behaviour precisely; each TEST_CASE below names the Go test it replaces.
//
// Where Go points dockerHubTagsURL at an httptest server and inspects the request
// path it received, this substitutes cup.platform's http_get and inspects the URL
// that was asked for — the same assertion, one layer up. See the note at the top of
// scaffold_releases_test.cpp.

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import cup.scaffold;
import cup.platform;

namespace {

// File-scope for the same reason as in scaffold_releases_test.cpp: HttpGet is a
// plain function pointer, so a substituted fetcher cannot capture.
struct TagsStub {
    std::string body;
    bool fail = false;
    std::string url;
};

TagsStub& stub() {
    static TagsStub state;
    return state;
}

[[nodiscard]] std::expected<std::string, utils::error::Error> stub_get(std::string_view url) {
    stub().url = url;
    if (stub().fail) {
        return std::unexpected(utils::error::Error("GET failed: 404 Not Found"));
    }
    return stub().body;
}

class ScopedTagsBody {
public:
    explicit ScopedTagsBody(std::string body, bool fail = false) {
        stub() = TagsStub{.body = std::move(body), .fail = fail};
    }
    ScopedTagsBody(const ScopedTagsBody&) = delete;
    ScopedTagsBody& operator=(const ScopedTagsBody&) = delete;
    ~ScopedTagsBody() { stub() = TagsStub{}; }

private:
    cup::platform::ScopedHttpGet installed_{stub_get};
};

}  // namespace

// Go: TestParseDockerHubTags
TEST_CASE("parse_docker_hub_tags keeps the API's order", "[scaffold][dockerhub]") {
    constexpr std::string_view kBody =
        R"({"count":3,"results":[{"name":"14"},{"name":"13-bookworm"},{"name":""},{"name":"latest"}]})";
    // Order preserved (the API sorts newest-first), blank names dropped.
    REQUIRE(cup::scaffold::detail::parse_docker_hub_tags(kBody) ==
            std::vector<std::string>{"14", "13-bookworm", "latest"});

    // Malformed JSON yields no tags rather than throwing.
    REQUIRE(cup::scaffold::detail::parse_docker_hub_tags("not json").empty());
    REQUIRE(cup::scaffold::detail::parse_docker_hub_tags(R"({"results":"not an array"})").empty());
}

// Go: TestDockerHubTagsUsesFunc
TEST_CASE("docker_hub_tags reads the installed source", "[scaffold][dockerhub]") {
    const cup::scaffold::ScopedDockerHubTags scoped(
        [](std::string_view repo) -> std::expected<std::vector<std::string>, utils::error::Error> {
            return std::vector<std::string>{"stub:" + std::string(repo)};
        });

    const auto tags = cup::scaffold::docker_hub_tags("gcc");
    REQUIRE(tags.has_value());
    REQUIRE(*tags == std::vector<std::string>{"stub:gcc"});
}

// Go: TestFetchDockerHubTags
TEST_CASE("fetch_docker_hub_tags normalises the repo into the URL", "[scaffold][dockerhub]") {
    const ScopedTagsBody body(R"({"results":[{"name":"14"},{"name":"13"}]})");

    const auto tags = cup::scaffold::detail::fetch_docker_hub_tags("gcc");
    REQUIRE(tags.has_value());
    REQUIRE(*tags == std::vector<std::string>{"14", "13"});
    // The bare repo was resolved into the library namespace before the request.
    INFO(stub().url);
    REQUIRE(stub().url.contains("library/gcc"));
    REQUIRE(stub().url.starts_with("https://hub.docker.com/v2/repositories/"));
}

// Go: TestFetchDockerHubTagsHTTPError
TEST_CASE("a failed request surfaces as an error, not empty tags", "[scaffold][dockerhub]") {
    const ScopedTagsBody failing("", /*fail=*/true);
    REQUIRE_FALSE(cup::scaffold::detail::fetch_docker_hub_tags("gcc").has_value());
}

// Go: TestNormalizeRepo
TEST_CASE("normalize_repo resolves official images", "[scaffold][dockerhub]") {
    const std::vector<std::pair<std::string_view, std::string_view>> cases{
        {"gcc", "library/gcc"},
        {"debian", "library/debian"},
        {"silkeh/clang", "silkeh/clang"},  // already namespaced: left alone
        {"  ubuntu  ", "library/ubuntu"},
        {"/library/gcc/", "library/gcc"},
    };
    for (const auto& [repo, want] : cases) {
        INFO("normalize_repo(" << repo << ")");
        REQUIRE(cup::scaffold::detail::normalize_repo(repo) == want);
    }
}
