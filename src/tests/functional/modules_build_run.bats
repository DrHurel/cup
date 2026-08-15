#!/usr/bin/env bats
# Black-box coverage for cup's *modules* code path -- everything in
# build_run.bats exercises fixture/ (cpp_standard=17, so uses_modules() is
# false and `cup add lib`/`add test` scaffold #include-based headers).
# fixture-modules/ pins cpp_standard=23, std_module=false (named modules
# without `import std`, the same combination this repo itself builds with),
# so the scenarios here drive real `export module` .cppm sources and a
# genuine `import <lib>;` through cmake+ninja's module dependency scanning --
# not just #include -- to prove that code path actually compiles end to end.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"
FIXTURE="${BATS_TEST_DIRNAME}/fixture-modules"

setup() {
    PROJECT_DIR="${BATS_TEST_TMPDIR}/project"
    cp -R "$FIXTURE" "$PROJECT_DIR"
    cd "$PROJECT_DIR"
}

@test "add app, build, run, and clean a real modules project end to end" {
    run bash -c "printf 'hello\n\n' | '$CUP_BIN' add app"
    [ "$status" -eq 0 ]
    [ -f src/apps/hello/hello.cpp ]
    [ -f src/apps/hello/CMakeLists.txt ]
    grep -q "add_subdirectory(hello)" src/apps/CMakeLists.txt

    run "$CUP_BIN" build
    # TEMP DEBUG: root-causing a CI-only failure that doesn't reproduce
    # locally or in a matched ubuntu:24.04 container -- remove once diagnosed.
    if [ "$status" -ne 0 ]; then echo "DEBUG build status=$status output: $output"; fi
    [ "$status" -eq 0 ]
    [ -f build/Debug/bin/hello ]

    run "$CUP_BIN" run
    [ "$status" -eq 0 ]

    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    [ ! -d build ]
}

@test "add lib scaffolds a named-module class lib and cup build compiles it" {
    # Same non-interactive answers as the headers fixture's equivalent test:
    # lib name, then a blank line each for the numbered "template kind?"
    # (defaults to 1=class) and the "primary symbol name?" prompt (defaults
    # to Mylib). uses_modules()==true here, so this exercises create_lib_at's
    # modules branch: a primary aggregator (mylib.cppm, `export module
    # mylib;`) plus a partition (Mylib.cppm) in one FILE_SET CXX_MODULES.
    run bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib"
    [ "$status" -eq 0 ]
    [ -f src/libs/mylib/mylib.cppm ]
    [ -f src/libs/mylib/Mylib.cppm ]
    grep -q "export module mylib;" src/libs/mylib/mylib.cppm
    grep -q "add_subdirectory(mylib)" src/libs/CMakeLists.txt

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    [ -f build/Debug/lib/libmylib.a ]
}

@test "add test that imports a lib builds and passes via cup test" {
    bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib" >/dev/null

    # choose_test_module lists libs numbered after "[none]": 1=[none], 2=mylib.
    run bash -c "printf 'mylib_test\n2\n' | '$CUP_BIN' add test"
    [ "$status" -eq 0 ]
    [ -f src/tests/mylib_test.cpp ]
    grep -q '^import mylib;' src/tests/mylib_test.cpp
    grep -q 'target_link_libraries(mylib_test PRIVATE mylib)' src/tests/CMakeLists.txt

    run "$CUP_BIN" test
    [ "$status" -eq 0 ]
    [[ "$output" == *"mylib_test"* ]]
}
