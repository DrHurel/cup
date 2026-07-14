# Sample C++ toolchain image for `cup compiler verify` / `cup compiler set`.
#
# cup compiles a project inside this image to prove it still builds on the
# minimum toolchain before committing a compiler-floor change. Point a project's
# cup.toml at an image like this:
#
#   [compiler]
#   verify_image = "cup-cxx:latest"
#
# Build it, then verify:
#
#   docker build -t cup-cxx -f docker/toolchain.Dockerfile .
#   cup compiler verify                 # compiles the project in cup-cxx
#   cup compiler set gcc 14             # docker-verified; reverted if it fails
#
# This example carries GCC + CMake + Ninja from Debian's archive, which suits the
# headers family (C++11/14/17). C++20/23 modules need newer tools — a CMake >=
# 3.30 and GCC 15 (for `import std;`); base such an image on a distro/PPA that
# ships them and keep the tag in verify_image pointed at the floor you enforce.
FROM debian:trixie-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++ clang cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*

# cup mounts the project read-only at /work and builds in a container-local dir,
# so no WORKDIR or entrypoint is needed here.
#
# This is a standalone example you can point `verify_image` at. In a cup project
# you usually don't hand-write toolchain images: `cup new` creates a managed
# default build image at docker/<name>/Dockerfile, and registering an apt
# dependency (`cup register` -> apt-install) makes cup add that package to it
# automatically. See "Docker build images" in the README.
