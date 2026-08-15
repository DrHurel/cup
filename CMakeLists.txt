# Named modules need CMake >= 3.28.
cmake_minimum_required(VERSION 3.28)

# CUP_VERSION is this project's own cup.toml cup_version key — the single
# source of truth for cup's own version, read here rather than duplicated as
# a literal so `project()`, `cup --version`, and the version New.cpp stamps
# into freshly scaffolded projects can never drift apart.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cup.toml" _cup_version_line REGEX "^cup_version[ \t]*=")
if(NOT _cup_version_line)
  message(FATAL_ERROR "cup: cup.toml is missing a cup_version key")
endif()
string(REGEX REPLACE "^cup_version[ \t]*=[ \t]*\"([^\"]*)\".*$" "\\1" CUP_VERSION "${_cup_version_line}")

project(cup VERSION ${CUP_VERSION} LANGUAGES C CXX)

# >>> cup:compiler-guard >>>
# Minimum compiler versions, managed by `cup compiler`. Building with an older
# toolchain stops here instead of failing deep in a compile. Change a floor with
# `cup compiler set gcc|clang <version>` (docker-verified before it is committed).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
  message(FATAL_ERROR "cup: this project requires GCC >= 15 (have ${CMAKE_CXX_COMPILER_VERSION}). Lower the floor with `cup compiler set gcc <version>`.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
  message(FATAL_ERROR "cup: this project requires Clang >= 18 (have ${CMAKE_CXX_COMPILER_VERSION}). Lower the floor with `cup compiler set clang <version>`.")
endif()
# <<< cup:compiler-guard <<<

# C++23 with named modules but WITHOUT `import std;` — what cup.toml records as
# `cpp_standard = 23` + `std_module = false`.
#
# The two are separate capabilities: named modules need GCC 14 (cup's floor is
# GCC 15 — see docs/migration-cpp23.md's Phase 4 note on why it moved up from
# 14 — but that was forced by a different GCC 14 modules bug, not by this
# decision), while `import std;` needs GCC 15 and CMake 3.30 behind an
# experimental gate. Declining the std module keeps std::expected and
# std::print — standard-library *features*, not std-module ones — available
# without opting into that experimental gate. cup honours std_module, so
# `cup add` keeps scaffolding global-module-fragment sources at this standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # -std=c++23, not -std=gnu++23

# Emit compile_commands.json so external tooling (clangd, clang-tidy, sonar) can
# see exactly how each translation unit is compiled.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Collect build results under the build tree so each mode's artifacts are tidy:
# with a per-mode build dir (build/<mode>) this puts binaries in build/<mode>/bin
# and libraries in build/<mode>/lib.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Build configurations: Debug, Release, and Coverage are always available. ---
set(CUP_BUILD_TYPES Debug Release Coverage)

get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_is_multi_config)
  set(CMAKE_CONFIGURATION_TYPES "${CUP_BUILD_TYPES}" CACHE STRING
      "Available build configurations" FORCE)
else()
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${CUP_BUILD_TYPES})
endif()

# Coverage build: instrument with gcov so a report can be generated.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(_cup_coverage_flags "-O0 -g --coverage")
  # -fprofile-abs-path records absolute source paths in the .gcno files, so gcov
  # can resolve a translation unit's source from any working directory. The Sonar
  # scan needs that: it runs gcovr from the repository root rather than from the
  # build tree, because --root decides the paths written into the report and Sonar
  # keys these files as src/... . GCC-only — Clang has no such flag.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND _cup_coverage_flags " -fprofile-abs-path")
  endif()
  foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_COVERAGE "${_cup_coverage_flags}" CACHE STRING
        "Flags used by the ${lang} compiler for the Coverage build" FORCE)
  endforeach()
  foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_COVERAGE "--coverage" CACHE STRING
        "Linker flags for the Coverage build" FORCE)
  endforeach()
  mark_as_advanced(
    CMAKE_C_FLAGS_COVERAGE
    CMAKE_CXX_FLAGS_COVERAGE
    CMAKE_EXE_LINKER_FLAGS_COVERAGE
    CMAKE_SHARED_LINKER_FLAGS_COVERAGE
    CMAKE_MODULE_LINKER_FLAGS_COVERAGE
  )
endif()

include(CTest)
enable_testing()

# Embed the template corpus in the binary — the C++ replacement for the Go
# implementation's `//go:embed all:files`. This is the one piece of build
# machinery cup cannot scaffold for itself; see cmake/EmbedTemplates.cmake.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EmbedTemplates)
cup_embed_templates(TARGET cup_templates DIR "${CMAKE_CURRENT_SOURCE_DIR}/templates")

# --- Version info, embedded for `cup --version` ----------------------------
# CUP_BUILD_SHA is empty unless set on the command line. CI's
# build-static.yml passes it (from $GITHUB_SHA) when it builds the binary
# that CI verifies and releases publish; a plain `cup build`/devtools/build.sh
# never sets it. That distinction is what lets `cup --version` show a build
# SHA only for a binary GitHub Actions actually built.
set(CUP_BUILD_SHA "" CACHE STRING "Git commit SHA this binary was built from (set by CI)")
set(_cup_version_gen_dir "${CMAKE_BINARY_DIR}/generated/cup_version")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
  "${_cup_version_gen_dir}/cup/version.h"
  @ONLY)
add_library(cup_version INTERFACE)
target_include_directories(cup_version INTERFACE "${_cup_version_gen_dir}")

add_subdirectory(third_party)
add_subdirectory(src/libs)
add_subdirectory(src/apps)
add_subdirectory(src/tests)
