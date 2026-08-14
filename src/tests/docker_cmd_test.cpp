#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

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
import cup.platform;
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

class ScopedCwd {
public:
    explicit ScopedCwd(const std::filesystem::path& dir)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~ScopedCwd() { std::filesystem::current_path(previous_); }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;

private:
    std::filesystem::path previous_;
};

class StubRunCommand {
public:
    explicit StubRunCommand(bool fail = false)
        : fail_(fail),
          override_(cup::platform::run_command_func(),
                    cup::platform::RunCommandFunc{
                        [this](const std::filesystem::path&, std::string_view name,
                              std::span<const std::string> args)
                            -> std::expected<void, cup::error::Error> {
                            std::string line(name);
                            for (const auto& arg : args) {
                                line += ' ';
                                line += arg;
                            }
                            calls_.push_back(std::move(line));
                            if (fail_) {
                                return std::unexpected(cup::error::Error("stubbed failure"));
                            }
                            return {};
                        }}) {}

    [[nodiscard]] const std::vector<std::string>& calls() const { return calls_; }

private:
    bool fail_;
    std::vector<std::string> calls_;
    ScopedOverride<cup::platform::RunCommandFunc> override_;
};

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Project make_project(const TempDir& dir, const DockerConfig& docker = {}) {
    Config cfg{.name = "demo", .cpp_standard = 20, .build_tool = "cmake", .docker = docker};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    return Project{dir.path(), cfg};
}

std::expected<cup::scaffold::Tags, cup::error::Error> stub_tags_ok(std::string_view) {
    return cup::scaffold::Tags{{"trixie-slim"}};
}

}

TEST_CASE("next_version", "[cmd][docker]") {
    REQUIRE(cup::cmd::next_version(0, "", "h1") == 1);
    REQUIRE(cup::cmd::next_version(3, "h1", "h1") == 3);
    REQUIRE(cup::cmd::next_version(3, "h1", "h2") == 4);
}

TEST_CASE("image_tag", "[cmd][docker]") { REQUIRE(cup::cmd::image_tag("myproj", 3) == "myproj:3"); }

TEST_CASE("select_images", "[cmd][docker]") {
    const TempDir dir;

    SECTION("no images defined is an error") {
        Project proj = make_project(dir);
        REQUIRE_FALSE(cup::cmd::select_images(proj, {}).has_value());
    }

    const DockerConfig docker{
        .images = {DockerImage{.name = "runtime", .base = "debian:trixie-slim"},
                   DockerImage{.name = "dev", .base = "gcc:15"}}};
    Project proj = make_project(dir, docker);

    SECTION("no name selects every image") {
        auto got = cup::cmd::select_images(proj, {});
        REQUIRE(got.has_value());
        REQUIRE(got->size() == 2);
    }
    SECTION("an explicit name selects just that image") {
        auto got = cup::cmd::select_images(proj, std::vector<std::string>{"dev"});
        REQUIRE(got.has_value());
        REQUIRE(got->size() == 1);
        REQUIRE((*got)[0]->name == "dev");
    }
    SECTION("an unknown name is an error") {
        REQUIRE_FALSE(cup::cmd::select_images(proj, std::vector<std::string>{"bogus"}).has_value());
    }
}

TEST_CASE("docker_new scaffolds an image and refuses a duplicate name", "[cmd][docker]") {
    const TempDir dir;
    Project proj = make_project(dir);
    const ScopedOverride tags_override(cup::scaffold::docker_hub_tags_func(),
                                       cup::scaffold::DockerHubTagsFunc{&stub_tags_ok});

    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("runtime\ndebian\n1\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::docker_new(proj).has_value());
    }
    REQUIRE(std::filesystem::exists(cup::cmd::dockerfile_path(proj, "runtime")));
    REQUIRE(proj.config.docker.find("runtime") != nullptr);

    // A duplicate (even differently-cased) name is refused before prompting
    // for a base image again.
    const ScopedStdin not_a_terminal("");
    std::istringstream in("Runtime\n");
    const cup::ui::ScopedInput scoped(in);
    REQUIRE_FALSE(cup::cmd::docker_new(proj).has_value());
}

TEST_CASE("build_image bumps the version only when the Dockerfile content changed",
          "[cmd][docker]") {
    const TempDir dir;
    const DockerConfig docker{.images = {DockerImage{.name = "runtime", .base = "debian:trixie-slim"}}};
    Project proj = make_project(dir, docker);
    dir.write("docker/runtime/Dockerfile", "FROM debian:trixie-slim\n");
    StubRunCommand stub;

    REQUIRE(cup::cmd::build_image(proj, proj.config.docker.images[0]).has_value());
    REQUIRE(proj.config.docker.images[0].version == 1);
    const std::string hash1 = proj.config.docker.images[0].hash;
    REQUIRE(hash1.size() == 64);

    REQUIRE(cup::cmd::build_image(proj, proj.config.docker.images[0]).has_value());
    REQUIRE(proj.config.docker.images[0].version == 1);
    REQUIRE(proj.config.docker.images[0].hash == hash1);

    dir.write("docker/runtime/Dockerfile", "FROM debian:trixie-slim\nRUN echo hi\n");
    REQUIRE(cup::cmd::build_image(proj, proj.config.docker.images[0]).has_value());
    REQUIRE(proj.config.docker.images[0].version == 2);
    REQUIRE(proj.config.docker.images[0].hash != hash1);

    REQUIRE(stub.calls().size() == 3);
    for (const auto& call : stub.calls()) {
        REQUIRE(call.starts_with("docker build"));
    }
}

TEST_CASE("build_image reports a missing Dockerfile", "[cmd][docker]") {
    const TempDir dir;
    const DockerConfig docker{.images = {DockerImage{.name = "runtime", .base = "debian:trixie-slim"}}};
    Project proj = make_project(dir, docker);
    REQUIRE_FALSE(cup::cmd::build_image(proj, proj.config.docker.images[0]).has_value());
}

TEST_CASE("run_docker_build persists version/hash to cup.toml", "[cmd][docker]") {
    const TempDir dir;
    const DockerConfig docker{.images = {DockerImage{.name = "runtime", .base = "debian:trixie-slim"}}};
    Project proj = make_project(dir, docker);
    dir.write("docker/runtime/Dockerfile", "FROM debian:trixie-slim\n");
    StubRunCommand stub;

    REQUIRE(cup::cmd::run_docker_build(proj, {}).has_value());
    const auto toml = read_whole_file(dir.path() / "cup.toml");
    REQUIRE(toml.has_value());
    REQUIRE(toml->find("version = 1") != std::string::npos);
}

TEST_CASE("run_docker_push refuses an unbuilt image", "[cmd][docker]") {
    const TempDir dir;
    const DockerConfig docker{.images = {DockerImage{.name = "runtime", .base = "debian:trixie-slim"}}};
    Project proj = make_project(dir, docker);
    proj.config.docker.registry = "docker.io/someone";
    StubRunCommand stub;

    REQUIRE_FALSE(cup::cmd::run_docker_push(proj, {}).has_value());
    REQUIRE(stub.calls().empty());
}

TEST_CASE("run_docker_push prompts for and saves a registry, then tags and pushes",
          "[cmd][docker]") {
    const TempDir dir;
    DockerConfig docker{.images = {DockerImage{.name = "runtime", .base = "debian:trixie-slim",
                                               .version = 1, .hash = "abc"}}};
    Project proj = make_project(dir, docker);
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("docker.io/someone/\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::run_docker_push(proj, {}).has_value());
    REQUIRE(proj.config.docker.registry == "docker.io/someone");
    // Each image is pushed under both its version and :latest tags.
    REQUIRE(stub.calls() ==
            std::vector<std::string>{"docker tag runtime:1 docker.io/someone/runtime:1",
                                     "docker push docker.io/someone/runtime:1",
                                     "docker tag runtime:latest docker.io/someone/runtime:latest",
                                     "docker push docker.io/someone/runtime:latest"});

    const auto toml = read_whole_file(dir.path() / "cup.toml");
    REQUIRE(toml.has_value());
    REQUIRE(toml->find("registry = \"docker.io/someone\"") != std::string::npos);
}

TEST_CASE("run_docker dispatches and rejects a missing or unknown subcommand", "[cmd][docker]") {
    const TempDir dir;
    make_project(dir);
    const ScopedCwd cwd(dir.path());

    REQUIRE_FALSE(cup::cmd::run_docker({}).has_value());
    REQUIRE_FALSE(cup::cmd::run_docker(std::vector<std::string>{"bogus"}).has_value());
    // build/push against a project with no images fail before reaching docker.
    REQUIRE_FALSE(cup::cmd::run_docker(std::vector<std::string>{"build"}).has_value());
    REQUIRE_FALSE(cup::cmd::run_docker(std::vector<std::string>{"push"}).has_value());
}

TEST_CASE("run_docker outside a project reports project::find's error", "[cmd][docker]") {
    const TempDir dir;
    const ScopedCwd cwd(dir.path());
    REQUIRE_FALSE(cup::cmd::run_docker({}).has_value());
}
