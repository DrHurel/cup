# Third-party dependencies, registered via `cup register`.
# git submodules -> add_subdirectory, cmake downloads -> FetchContent,
# system packages -> find_package.

include(FetchContent)

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.9.1
)
FetchContent_MakeAvailable(Catch2)
# GLOBAL: find_package's imported targets are scoped to this directory (and its
# children) by default, but cup.platform needing CURL::libcurl lives under
# src/libs, a sibling of third_party — without GLOBAL the target is invisible
# there.
find_package(CURL REQUIRED GLOBAL) # cup-apt: libcurl4-openssl-dev
