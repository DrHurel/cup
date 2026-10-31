#!/usr/bin/env bats
# End-to-end black-box test of `cup init`: the compiled cup binary adopting
# an existing project in place, then managing it — `cup add`, then a real
# cmake + ninja (or make) build and run of the result. Network-independent,
# like build_run.bats: init never queries Docker Hub or compiler releases.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"

setup() {
    PROJECT_DIR="${BATS_TEST_TMPDIR}/legacy"
    mkdir -p "$PROJECT_DIR"
    cd "$PROJECT_DIR"
}

@test "init adopts an existing CMake project, which cup then builds and runs" {
    cat > CMakeLists.txt <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(legacy LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(legacy_tool tool.cpp)
CMAKE
    printf 'int main() { return 0; }\n' > tool.cpp

    run "$CUP_BIN" init
    [ "$status" -eq 0 ]
    [[ "$output" == *"detected c++17"* ]]
    grep -q 'name = "legacy"' cup.toml
    grep -q 'cpp_standard = 17' cup.toml
    grep -q '# >>> cup:compiler-guard >>>' CMakeLists.txt
    grep -q 'add_subdirectory(src/apps)' CMakeLists.txt
    # The project's own target and source are untouched.
    grep -q 'add_executable(legacy_tool tool.cpp)' CMakeLists.txt

    run bash -c "printf 'hello\n\n' | '$CUP_BIN' add app"
    [ "$status" -eq 0 ]

    # The adopted CMakeLists sets no CMAKE_RUNTIME_OUTPUT_DIRECTORY, so the
    # app lands in CMake's default spot — cup run finds it there.
    run "$CUP_BIN" run hello
    [ "$status" -eq 0 ]
    [[ "$output" == *"Hello from hello!"* ]]
    [ -f build/Debug/src/apps/hello/hello ]
    [ -f build/Debug/legacy_tool ]

    run "$CUP_BIN" compiler set gcc 9 --no-verify
    [ "$status" -eq 0 ]
    grep -q 'VERSION_LESS 9' CMakeLists.txt
}

@test "init adopts a Makefile project and cup build runs that Makefile" {
    printf 'CXXFLAGS = -std=c++14\nall:\n\t@echo built-by-make $(MODE)\n' > Makefile

    run "$CUP_BIN" init mk
    [ "$status" -eq 0 ]
    grep -q 'build_tool = "make"' cup.toml
    grep -q 'cpp_standard = 14' cup.toml

    run "$CUP_BIN" build Release
    [ "$status" -eq 0 ]
    [[ "$output" == *"built-by-make Release"* ]]
}

@test "init in an empty directory scaffolds a fresh tree that builds" {
    run bash -c "printf '1\n' | '$CUP_BIN' init fresh --std 20"
    [ "$status" -eq 0 ]
    [ -f CMakeLists.txt ]
    [ -f src/apps/CMakeLists.txt ]

    run bash -c "printf 'hello\n\n' | '$CUP_BIN' add app"
    [ "$status" -eq 0 ]
    run "$CUP_BIN" run hello
    [ "$status" -eq 0 ]
    [[ "$output" == *"Hello from hello!"* ]]
}

@test "init refuses a directory that is already a cup project" {
    printf 'name = "taken"\n' > cup.toml
    run "$CUP_BIN" init
    [ "$status" -eq 1 ]
    [[ "$output" == *"already a cup project"* ]]
}
