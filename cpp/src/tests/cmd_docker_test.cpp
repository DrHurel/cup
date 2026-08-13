#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TempDir.hpp"

import cup.cmd;
import cup.project;
import cup.scaffold;
import cup.ui;

namespace {

using cup::project::Config;
using cup::project::DockerConfig;
using cup::project::DockerImage;
using cup::project::Project;
using cup::test::TempDir;

class ScopedStdin {
public:
    explicit ScopedStdin(std::string_view keys) {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::write(fds[1], keys.data(), keys.size()) == static_cast<ssize_t>(keys.size()));
        REQUIRE(::close(fds[1]) == 0);
        REQUIRE(::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO);
        REQUIRE(::close(fds[0]) == 0);
    }
    ScopedStdin(const ScopedStdin&) = delete;
    ScopedStdin& operator=(const ScopedStdin&) = delete;
    ~ScopedStdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
    }

private:
    int saved_ = -1;
};

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

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Project make_project(const TempDir& dir, std::string_view tool, const DockerConfig& docker) {
    Config cfg{.name = "demo", .cpp_standard = 23, .build_tool = std::string(tool), .docker = docker};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    return Project{dir.path(), cfg};
}

std::expected<cup::scaffold::Tags, cup::error::Error> stub_tags_ok(std::string_view) {
    return cup::scaffold::Tags{{"latest", "3.23", "3.22"}};
}

std::expected<cup::scaffold::Tags, cup::error::Error> stub_tags_empty(std::string_view) {
    return cup::scaffold::Tags{{}};
}

}

TEST_CASE("render_build_dockerfile", "[cmd][docker]") {
    SECTION("no packages: just the base image") {
        const std::string out = cup::cmd::render_build_dockerfile("debian:stable-slim", {});
        REQUIRE(out.find("FROM debian:stable-slim\n") != std::string::npos);
        REQUIRE(out.find("apt-get") == std::string::npos);
    }
    SECTION("with packages: an apt layer listing each one") {
        const std::vector<std::string> pkgs{"libcurl4-openssl-dev", "pkg-config"};
        const std::string out = cup::cmd::render_build_dockerfile("debian:stable-slim", pkgs);
        REQUIRE(out.find("FROM debian:stable-slim\n") != std::string::npos);
        REQUIRE(out.find("RUN apt-get update") != std::string::npos);
        REQUIRE(out.find("libcurl4-openssl-dev") != std::string::npos);
        REQUIRE(out.find("pkg-config") != std::string::npos);
    }
}

TEST_CASE("docker_image_dir and dockerfile_path", "[cmd][docker]") {
    const TempDir dir;
    const Project proj = make_project(dir, "", DockerConfig{});
    REQUIRE(cup::cmd::docker_image_dir(proj, "demo") == dir.path() / "docker" / "demo");
    REQUIRE(cup::cmd::dockerfile_path(proj, "demo") ==
            dir.path() / "docker" / "demo" / "Dockerfile");
}

TEST_CASE("apt_packages scans the third-party file for its build tool, deduplicated",
          "[cmd][docker]") {
    SECTION("cmake project scans third_party/CMakeLists.txt") {
        const TempDir dir;
        const Project proj = make_project(dir, "", DockerConfig{});
        dir.write("third_party/CMakeLists.txt",
                  "# cup-dep: curl\n"
                  "# cup-apt: libcurl4-openssl-dev pkg-config\n"
                  "# cup-apt: libcurl4-openssl-dev\n");
        REQUIRE(cup::cmd::apt_packages(proj) ==
                std::vector<std::string>{"libcurl4-openssl-dev", "pkg-config"});
    }
    SECTION("make project scans third_party/third_party.mk") {
        const TempDir dir;
        const Project proj = make_project(dir, "make", DockerConfig{});
        dir.write("third_party/third_party.mk", "# cup-apt: zlib1g-dev\n");
        REQUIRE(cup::cmd::apt_packages(proj) == std::vector<std::string>{"zlib1g-dev"});
    }
    SECTION("no registrations yet: empty, not an error") {
        const TempDir dir;
        const Project proj = make_project(dir, "", DockerConfig{});
        REQUIRE(cup::cmd::apt_packages(proj).empty());
    }
}

TEST_CASE("sync_default_build_image", "[cmd][docker]") {
    SECTION("no default image: a no-op") {
        const TempDir dir;
        const Project proj = make_project(dir, "", DockerConfig{});
        REQUIRE(cup::cmd::sync_default_build_image(proj).has_value());
        REQUIRE_FALSE(std::filesystem::exists(dir.path() / "docker"));
    }
    SECTION("a default image: writes the Dockerfile, then leaves it unchanged on a repeat call") {
        const TempDir dir;
        const DockerConfig docker{
            .images = {DockerImage{.name = "demo", .base = "debian:stable-slim", .is_default = true}}};
        const Project proj = make_project(dir, "", docker);

        REQUIRE(cup::cmd::sync_default_build_image(proj).has_value());
        const auto path = dir.path() / "docker" / "demo" / "Dockerfile";
        const auto first = read_whole_file(path);
        REQUIRE(first.has_value());
        REQUIRE(first->find("FROM debian:stable-slim") != std::string::npos);

        REQUIRE(cup::cmd::sync_default_build_image(proj).has_value());
        REQUIRE(read_whole_file(path) == first);
    }
    SECTION("registered apt packages appear in the regenerated Dockerfile") {
        const TempDir dir;
        const DockerConfig docker{
            .images = {DockerImage{.name = "demo", .base = "debian:stable-slim", .is_default = true}}};
        const Project proj = make_project(dir, "", docker);
        dir.write("third_party/CMakeLists.txt", "# cup-apt: libcurl4-openssl-dev\n");

        REQUIRE(cup::cmd::sync_default_build_image(proj).has_value());
        const auto content = read_whole_file(dir.path() / "docker" / "demo" / "Dockerfile");
        REQUIRE(content.has_value());
        REQUIRE(content->find("libcurl4-openssl-dev") != std::string::npos);
    }
}

TEST_CASE("choose_image_tag picks from a numbered list when tags are available",
          "[cmd][docker]") {
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_tags_ok});
    const ScopedStdin not_a_terminal("");
    std::istringstream in("2\n");
    const cup::ui::ScopedInput scoped(in);

    auto tag = cup::cmd::choose_image_tag("debian");
    REQUIRE(tag.has_value());
    REQUIRE(*tag == "3.23");
}

TEST_CASE("choose_image_tag falls back to free text when no tags come back",
          "[cmd][docker]") {
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_tags_empty});
    const ScopedStdin not_a_terminal("");
    std::istringstream in("\n");
    const cup::ui::ScopedInput scoped(in);

    auto tag = cup::cmd::choose_image_tag("debian");
    REQUIRE(tag.has_value());
    REQUIRE(*tag == "latest");
}

TEST_CASE("choose_base_image joins the repo and chosen tag", "[cmd][docker]") {
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_tags_ok});
    const ScopedStdin not_a_terminal("");
    std::istringstream in("debian\n1\n");
    const cup::ui::ScopedInput scoped(in);

    auto base = cup::cmd::choose_base_image();
    REQUIRE(base.has_value());
    REQUIRE(*base == "debian:latest");
}
