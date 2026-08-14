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

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
  GIT_TAG v7.0.3
)
# Explicit rather than relying on the defaults: cup builds its own C++23
# modules already, and FTXUI's experimental C++20-modules build (off by
# default) has no reason to run alongside that. cup installs nothing, so
# skip FTXUI's install rules too.
set(FTXUI_BUILD_MODULES OFF)
set(FTXUI_ENABLE_INSTALL OFF)
FetchContent_MakeAvailable(ftxui)
# GLOBAL: find_package's/pkg_check_modules' imported targets are scoped to this
# directory (and its children) by default, but cup.platform needing
# CURL::libcurl lives under src/libs, a sibling of third_party — without GLOBAL
# the target is invisible there.
if(CURL_USE_STATIC_LIBS)
  # The musl-static release build (release-musl-static CI job) needs libcurl.a
  # plus its own static transitive deps (openssl, zlib, nghttp2, brotli — apk
  # installs the matching *-static packages). find_package(CURL)'s own
  # static-preference handling isn't reliable across CMake/distro versions (it
  # resolved Alpine's libcurl.so even with CURL_USE_STATIC_LIBS set), and
  # pkg_check_modules(... IMPORTED_TARGET ...) always builds its target from
  # the dynamic (non "--static") result variables regardless — there is no
  # keyword that switches it. So build the imported target by hand from the
  # PC_CURL_STATIC_* variables pkg-config always computes (libcurl.pc's
  # Libs.private, i.e. the whole static dependency chain). CMake only resolves
  # bare names to absolute paths for the primary (dynamic) library list —
  # PC_CURL_STATIC_LINK_LIBRARIES is empty — so link the bare
  # PC_CURL_STATIC_LIBRARIES names instead and let the linker's own -L search
  # (from PC_CURL_STATIC_LIBRARY_DIRS) resolve them, same as a plain
  # `pkg-config --static --libs libcurl` invocation would.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PC_CURL REQUIRED QUIET libcurl)
  add_library(CURL::libcurl INTERFACE IMPORTED GLOBAL)
  set_target_properties(CURL::libcurl PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${PC_CURL_STATIC_INCLUDE_DIRS}"
    INTERFACE_LINK_DIRECTORIES "${PC_CURL_STATIC_LIBRARY_DIRS}"
    INTERFACE_LINK_LIBRARIES "${PC_CURL_STATIC_LIBRARIES}"
    INTERFACE_LINK_OPTIONS "${PC_CURL_STATIC_LDFLAGS_OTHER}"
    INTERFACE_COMPILE_OPTIONS "${PC_CURL_STATIC_CFLAGS_OTHER}")
else()
  find_package(CURL REQUIRED GLOBAL) # cup-apt: libcurl4-openssl-dev
endif()
