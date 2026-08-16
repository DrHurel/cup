# Minimal fixture project for the bats functional suite (build_run.bats): a
# real, buildable CMake project driven end to end through the compiled cup
# binary. Deliberately plain (no cup module scaffolding, no compiler-version
# guard) since it exists to exercise cup's configure/build/test/run/clean
# dispatch and real cmake+ninja+ctest subprocesses, not cup's own templates.
cmake_minimum_required(VERSION 3.16)

project(fixture VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CTest)
enable_testing()

add_subdirectory(src/libs)
add_subdirectory(src/apps)
