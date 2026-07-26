# Named modules need CMake >= 3.28.
cmake_minimum_required(VERSION 3.28)

project(cup VERSION 0.1.0 LANGUAGES C CXX)

# >>> cup:compiler-guard >>>
# Minimum compiler versions, managed by `cup compiler`. Building with an older
# toolchain stops here instead of failing deep in a compile. Change a floor with
# `cup compiler set gcc|clang <version>` (docker-verified before it is committed).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "cup: this project requires GCC >= 14 (have ${CMAKE_CXX_COMPILER_VERSION}). Lower the floor with `cup compiler set gcc <version>`.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
  message(FATAL_ERROR "cup: this project requires Clang >= 18 (have ${CMAKE_CXX_COMPILER_VERSION}). Lower the floor with `cup compiler set clang <version>`.")
endif()
# <<< cup:compiler-guard <<<

# C++23 with named modules but WITHOUT `import std;`.
#
# This line is the migration's one hand edit, and it is deliberate: cup.toml still
# records cpp_standard = 20, because cup cannot yet express "C++23 without the std
# module" (scaffold.StdVars emits `import std;` unconditionally for std >= 23, and
# MinCompilers maps 23 -> GCC 15). Leaving cup.toml at 20 keeps `cup add` emitting
# global-module-fragment sources, which is the shape we want; raising the standard
# here makes them compile as C++23 so std::expected and std::print are available.
#
# The two settings do not fight: `cup add` writes per-target
# `target_compile_features(<t> PUBLIC cxx_std_20)`, which is a *minimum*, so targets
# still build at the 23 set here.
#
# Closing that gap in the C++ cup — a first-class "C++23, no std module" option —
# is post-relay work; see docs/migration-cpp23.md.
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
  foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_COVERAGE "-O0 -g --coverage" CACHE STRING
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

add_subdirectory(third_party)
add_subdirectory(src/libs)
add_subdirectory(src/apps)
