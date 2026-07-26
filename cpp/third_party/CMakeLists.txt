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
find_package(CURL REQUIRED) # cup-apt: libcurl4-openssl-dev
