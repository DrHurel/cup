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

// Declarations only, defined in Docker.cpp. Only the slice `cup new` needs is
// here so far: the default build image's Dockerfile stays in sync with the
// project's base image and (once `cup register` exists) its apt dependencies.
// `cup docker new|build|push` and `cup register`/`cup unregister` land in a
// later group of the migration and extend this same partition.
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

}
