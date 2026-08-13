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
