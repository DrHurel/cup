#!/usr/bin/env bats
# End-to-end black-box test of the compiled cup binary against a real project:
# add an app, then configure/build/test/run/clean it through cup, driving the
# real system cmake + ninja + ctest the way a user's shell would. This is
# deliberately network-independent (no `cup new`, which needs live Docker Hub
# / compiler-release lookups covered instead by the stubbed Catch2 suites) —
# the fixture/ directory is a hand-written, already-valid cup project, copied
# fresh into a scratch dir per test so builds never interact across tests.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"
FIXTURE="${BATS_TEST_DIRNAME}/fixture"

setup() {
    PROJECT_DIR="${BATS_TEST_TMPDIR}/project"
    cp -R "$FIXTURE" "$PROJECT_DIR"
    cd "$PROJECT_DIR"
}

@test "add app, build, test, run, and clean a real project end to end" {
    run bash -c "printf 'hello\n\n' | '$CUP_BIN' add app"
    [ "$status" -eq 0 ]
    [ -f src/apps/hello/hello.cpp ]
    [ -f src/apps/hello/CMakeLists.txt ]
    grep -q "add_subdirectory(hello)" src/apps/CMakeLists.txt

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    [ -f build/Debug/bin/hello ]

    run "$CUP_BIN" test
    [ "$status" -eq 0 ]

    run "$CUP_BIN" run
    [ "$status" -eq 0 ]

    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    [ ! -d build ]
}

@test "run picks the sole app without prompting when only one exists" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run "$CUP_BIN" run
    [ "$status" -eq 0 ]
    [ -f build/Debug/bin/hello ]
}

@test "run with an explicit app name skips the picker" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run "$CUP_BIN" run Debug hello
    [ "$status" -eq 0 ]
}

@test "run with no apps reports the error instead of building" {
    run "$CUP_BIN" run
    [ "$status" -eq 1 ]
    [[ "$output" == *"no apps to run"* ]]
}

@test "test with no CMakeLists changes still configures and reports success" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run "$CUP_BIN" test
    [ "$status" -eq 0 ]
    [ -d build/Debug ]
}

@test "clean removes the build directory for every mode" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null
    "$CUP_BIN" build >/dev/null

    [ -d build ]
    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    [ ! -d build ]
}

@test "compiler show prints the floors pinned in cup.toml" {
    run "$CUP_BIN" compiler show
    [ "$status" -eq 0 ]
    [[ "$output" == *"gcc     >= 15"* ]]
    [[ "$output" == *"clang   >= 18"* ]]
}

@test "compiler set onto a marker-less CMakeLists fails and leaves cup.toml untouched" {
    # The fixture's CMakeLists carries no >>> cup:compiler-guard >>> markers
    # (it's hand-written, not cup-scaffolded), so the guard rewrite step
    # fails and the whole change must roll back.
    before="$(cat cup.toml)"
    run "$CUP_BIN" compiler set gcc 16 --no-verify
    [ "$status" -eq 1 ]
    [ "$(cat cup.toml)" = "$before" ]
}

@test "add lib scaffolds a header-only class lib and cup build compiles it" {
    # fixture/CMakeLists.txt add_subdirectory(src/libs) unconditionally
    # (src/libs/CMakeLists.txt starts as an empty placeholder, same as
    # src/apps/), matching a real `cup new` project's shape -- unlike apps,
    # `cup add lib` never touches the root CMakeLists itself.
    run bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib"
    [ "$status" -eq 0 ]
    [ -f src/libs/mylib/mylib.hpp ]
    [ -f src/libs/mylib/Mylib.h ]
    [ -f src/libs/mylib/Mylib.cpp ]
    grep -q "add_subdirectory(mylib)" src/libs/CMakeLists.txt

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    # The fixture's CMakeLists.txt is deliberately plain and doesn't set
    # CMAKE_ARCHIVE_OUTPUT_DIRECTORY, so the static lib lands under CMake's
    # default per-target dir rather than a shared build/<mode>/lib/.
    [ -f build/Debug/src/libs/mylib/libmylib.a ]
}

@test "add test linked to a lib builds and passes via cup test" {
    bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib" >/dev/null

    # choose_test_module lists libs numbered after "[none]": 1=[none], 2=mylib.
    run bash -c "printf 'mylib_test\n2\n' | '$CUP_BIN' add test"
    [ "$status" -eq 0 ]
    [ -f src/tests/mylib_test.cpp ]
    grep -q 'target_link_libraries(mylib_test PRIVATE mylib)' src/tests/CMakeLists.txt
    grep -q 'add_subdirectory(src/tests)' CMakeLists.txt

    run "$CUP_BIN" test
    [ "$status" -eq 0 ]
    [[ "$output" == *"mylib_test"* ]]
}

@test "configure alone generates the build tree without compiling a binary" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run "$CUP_BIN" configure
    [ "$status" -eq 0 ]
    [ -f build/Debug/CMakeCache.txt ]
    [ ! -f build/Debug/bin/hello ]
}

@test "rebuild wipes build/ then compiles from scratch" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null
    "$CUP_BIN" build >/dev/null
    [ -f build/Debug/bin/hello ]
    touch build/Debug/marker

    run "$CUP_BIN" rebuild
    [ "$status" -eq 0 ]
    [ ! -f build/Debug/marker ]
    [ -f build/Debug/bin/hello ]
}

@test "retest wipes build/ then reruns the test suite" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null
    "$CUP_BIN" test >/dev/null
    [ -d build/Debug ]
    touch build/Debug/marker

    run "$CUP_BIN" retest
    [ "$status" -eq 0 ]
    [ ! -f build/Debug/marker ]
}

@test "build with an unrecognized MODE token reports it as a stray argument" {
    run "$CUP_BIN" build Bogus
    [ "$status" -eq 1 ]
    [[ "$output" == *"unexpected argument(s): Bogus"* ]]
}

@test "run with an app name that doesn't exist reports the error and exits 1" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run "$CUP_BIN" run Debug bogus
    [ "$status" -eq 1 ]
    [[ "$output" == *'no such app "bogus"'* ]]
    [[ "$output" == *"hello"* ]]
}

@test "run forwards trailing args after -- to the program without erroring" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run "$CUP_BIN" run Debug hello -- --flag value
    [ "$status" -eq 0 ]
}

@test "a compile error in an app surfaces the failure and exits nonzero" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null
    printf 'int main() { return \n' > src/apps/hello/hello.cpp

    run "$CUP_BIN" build
    [ "$status" -eq 1 ]
    [[ "$output" == *"error:"* ]]
}

@test "clean before any build is a harmless no-op" {
    [ ! -d build ]
    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    [ ! -d build ]
}
