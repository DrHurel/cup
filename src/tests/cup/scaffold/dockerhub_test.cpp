#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TestHttpServer.hpp"

import cup.scaffold;

namespace {

using cup::test::TestHttpServer;

template <typename T>
class ScopedOverride {
public:
    ScopedOverride(T& slot, T value) : slot_(slot), previous_(slot) { slot_ = std::move(value); }
    ~ScopedOverride() { slot_ = std::move(previous_); }
    ScopedOverride(const ScopedOverride&) = delete;
    ScopedOverride& operator=(const ScopedOverride&) = delete;

private:
    T& slot_;
    T previous_;
};

}

TEST_CASE("parse_docker_hub_tags preserves order and drops blanks",
          "[dockerhub][parse_docker_hub_tags]") {
    const std::string body =
        R"({"count":3,"results":[{"name":"14"},{"name":"13-bookworm"},{"name":""},{"name":"latest"}]})";
    REQUIRE(cup::scaffold::detail::parse_docker_hub_tags(body) ==
            std::vector<std::string>{"14", "13-bookworm", "latest"});

    // Malformed JSON yields no tags rather than crashing.
    REQUIRE(cup::scaffold::detail::parse_docker_hub_tags("not json").empty());
}

TEST_CASE("docker_hub_tags goes through docker_hub_tags_func", "[dockerhub]") {
    const ScopedOverride override_func(
        cup::scaffold::docker_hub_tags_func(),
        cup::scaffold::DockerHubTagsFunc{+[](std::string_view repo)
                                             -> std::expected<cup::scaffold::Tags, cup::error::Error> {
            return cup::scaffold::Tags{{"stub:" + std::string(repo)}};
        }});

    const auto got = cup::scaffold::docker_hub_tags("gcc");
    REQUIRE(got.has_value());
    REQUIRE(got->values == std::vector<std::string>{"stub:gcc"});
}

// fetch_docker_hub_tags builds the request URL from docker_hub_tags_url_template
// (normalizing the repo into the library namespace) and parses the response, all
// against a local test server so it never touches the network.
TEST_CASE("fetch_docker_hub_tags", "[dockerhub][fetch]") {
    std::string got_path;
    const TestHttpServer server([&got_path](const std::string& path) {
        got_path = path;
        return TestHttpServer::Response{200, R"({"results":[{"name":"14"},{"name":"13"}]})"};
    });
    const ScopedOverride url(cup::scaffold::docker_hub_tags_url_template(), server.url() + "/%s/tags");

    const auto got = cup::scaffold::fetch_docker_hub_tags("gcc");
    REQUIRE(got.has_value());
    REQUIRE(got->values == std::vector<std::string>{"14", "13"});
    // The bare repo was normalized into the library namespace before the request.
    REQUIRE(got_path.find("library/gcc") != std::string::npos);
}

// A non-200 response surfaces as an error rather than empty tags.
TEST_CASE("fetch_docker_hub_tags on an HTTP error", "[dockerhub][fetch]") {
    const TestHttpServer server(
        [](const std::string&) { return TestHttpServer::Response{404, "nope"}; });
    const ScopedOverride url(cup::scaffold::docker_hub_tags_url_template(), server.url() + "/%s/tags");

    REQUIRE_FALSE(cup::scaffold::fetch_docker_hub_tags("gcc").has_value());
}

TEST_CASE("normalize_repo", "[dockerhub][normalize_repo]") {
    using cup::scaffold::detail::normalize_repo;
    REQUIRE(normalize_repo("gcc") == "library/gcc");
    REQUIRE(normalize_repo("debian") == "library/debian");
    REQUIRE(normalize_repo("silkeh/clang") == "silkeh/clang");
    REQUIRE(normalize_repo("  ubuntu  ") == "library/ubuntu");
    REQUIRE(normalize_repo("/library/gcc/") == "library/gcc");
}
