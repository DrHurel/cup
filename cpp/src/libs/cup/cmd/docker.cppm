module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module cup.cmd:docker;

export import cup.error;
export import cup.project;

// Declarations only, defined in Docker.cpp. `cup docker new|build|push`
// manages the project's build images, each living at docker/<name>/Dockerfile
// and versioned in cup.toml's [docker] table; the default image (created by
// `cup new`) additionally tracks the project's apt third-party dependencies.
export namespace cup::cmd {

[[nodiscard]] std::filesystem::path docker_image_dir(const project::Project& proj,
                                                      std::string_view name);
[[nodiscard]] std::filesystem::path dockerfile_path(const project::Project& proj,
                                                     std::string_view name);

// apt_packages scans the project's third-party registration file (empty for a
// fresh project — nothing is registered yet) for its "# cup-apt:" marker
// lines, so the default build image's Dockerfile can install what the
// project actually needs. Deduplicated, first-seen order.
[[nodiscard]] std::vector<std::string> apt_packages(const project::Project& proj);

// render_build_dockerfile builds the default image's Dockerfile: a bare
// `FROM base` plus, when the project registers apt dependencies, a layer
// that installs them.
[[nodiscard]] std::string render_build_dockerfile(std::string_view base,
                                                   std::span<const std::string> pkgs);

// sync_default_build_image regenerates the default build image's Dockerfile
// from its base and the currently registered apt packages. A no-op when the
// project has no default image, and it avoids rewriting an unchanged file so
// `cup docker build` only bumps the version on a real change.
[[nodiscard]] std::expected<void, error::Error> sync_default_build_image(
    const project::Project& proj);

// choose_base_image prompts for a Docker Hub repository and a tag, returning
// a "repo:tag" reference. Tags are fetched live so the user picks a real
// one; offline, it falls back to typing the tag by hand.
[[nodiscard]] std::expected<std::string, error::Error> choose_base_image();

// choose_image_tag lists repo's Docker Hub tags to pick from, capping the
// (newest first) list so the menu stays usable, and falling back to
// free-text entry when the tags cannot be fetched. Exposed for testing.
[[nodiscard]] std::expected<std::string, error::Error> choose_image_tag(std::string_view repo);

// select_images resolves the images a build/push acts on: the one named in
// args, or all of them when no name is given. The returned pointers alias
// proj.config.docker.images, so mutating an image through them and then
// calling persist_config makes the change stick.
[[nodiscard]] std::expected<std::vector<project::DockerImage*>, error::Error> select_images(
    project::Project& proj, std::span<const std::string> args);

// next_version advances an image's version only when its content changed: an
// unchanged, already-built image keeps its number, while a first build or a
// content change increments it.
[[nodiscard]] int next_version(int cur, std::string_view old_hash, std::string_view new_hash);

[[nodiscard]] std::string image_tag(std::string_view name, int version);

// persist_config writes the (mutated) project config back to cup.toml.
[[nodiscard]] std::expected<void, error::Error> persist_config(const project::Project& proj);

// docker_new scaffolds an additional, user-managed build image: a
// docker/<name>/ directory with a starter Dockerfile and a
// [[docker.image]] entry. Unlike the default image, cup does not rewrite
// this Dockerfile once written.
[[nodiscard]] std::expected<void, error::Error> docker_new(project::Project& proj);

// build_image regenerates the default image's Dockerfile (when img is the
// default), hashes it to decide whether the version should advance, and
// builds the image under its versioned and :latest tags.
[[nodiscard]] std::expected<void, error::Error> build_image(const project::Project& proj,
                                                             project::DockerImage& img);

[[nodiscard]] std::expected<void, error::Error> run_docker_build(project::Project& proj,
                                                                  std::span<const std::string> args);

[[nodiscard]] std::expected<void, error::Error> push_image(const project::Project& proj,
                                                            std::string_view registry,
                                                            const project::DockerImage& img);

[[nodiscard]] std::expected<void, error::Error> run_docker_push(project::Project& proj,
                                                                 std::span<const std::string> args);

[[nodiscard]] std::expected<void, error::Error> run_docker(std::span<const std::string> args);

}
