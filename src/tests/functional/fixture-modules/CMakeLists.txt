# Minimal fixture project for the bats functional suite (modules_build_run.bats):
# a real, buildable CMake project on cup's *modules* code path (cpp_standard=23,
# std_module=false -- named modules without `import std`, the same combination
# this repo itself builds with), driven end to end through the compiled cup
# binary. Deliberately plain (no compiler-version guard) since it exists to
# exercise cup's add/configure/build/test/run/clean dispatch against real
# `export module` / `import` sources, not cup's own templates.
cmake_minimum_required(VERSION 3.28)

project(fixture-modules VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include(CTest)
enable_testing()

add_subdirectory(src/libs)
add_subdirectory(src/apps)
