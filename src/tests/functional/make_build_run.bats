#!/usr/bin/env bats
# Black-box coverage for cup's *Make* build-tool code path -- build_run.bats
# and modules_build_run.bats both exercise build_tool="cmake" fixtures, so
# make_build (Build.cpp), the Make branches of Add.cpp/Thirdparty.cpp, and
# the discovery-based Makefile itself had zero functional coverage before
# this file. fixture-make/'s Makefile is a byte-for-byte copy of
# templates/make/project/Makefile.tmpl rendered at cpp_standard=17 (the
# fixture's std, matching what `cup new` would actually produce for this
# cup.toml) -- it discovers apps/libs/tests by *path*, so `cup add`
# deliberately writes no build-file wiring under Make (see Add.cpp's "Under
# Make the root Makefile discovers ... by path" comments) and this suite
# checks that discovery, not any generated wiring.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"
FIXTURE="${BATS_TEST_DIRNAME}/fixture-make"

setup() {
    PROJECT_DIR="${BATS_TEST_TMPDIR}/project"
    cp -R "$FIXTURE" "$PROJECT_DIR"
    cd "$PROJECT_DIR"
}

@test "add app, build, test, run, and clean a real Make project end to end" {
    run bash -c "printf 'hello\n\n' | '$CUP_BIN' add app"
    [ "$status" -eq 0 ]
    [ -f src/apps/hello/hello.cpp ]
    # Under Make, add app writes only the source -- no CMakeLists, no
    # parent-file registration; the Makefile finds it by path.
    [ ! -f src/apps/hello/CMakeLists.txt ]

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    [ -f build/Debug/bin/hello ]

    run "$CUP_BIN" run
    [ "$status" -eq 0 ]

    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    [ ! -d build ]
}

@test "add lib scaffolds a header-only lib and cup build archives it via the Makefile" {
    # create_header_lib_at's prompts (kind, then symbol) are the same
    # regardless of build tool -- only the post-write CMakeLists/parent-file
    # step is skipped under Make.
    run bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib"
    [ "$status" -eq 0 ]
    [ -f src/libs/mylib/mylib.hpp ]
    [ -f src/libs/mylib/Mylib.h ]
    [ -f src/libs/mylib/Mylib.cpp ]
    [ ! -f src/libs/mylib/CMakeLists.txt ]

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    # Unlike the plain CMake fixture, the Makefile explicitly sets
    # LIBDIR := $(BUILD)/lib, so the archive lands in a predictable place.
    [ -f build/Debug/lib/libmylib.a ]
}

@test "add test that includes a lib builds and passes via cup test (Make)" {
    bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib" >/dev/null

    # choose_test_module lists libs numbered after "[none]": 1=[none], 2=mylib.
    run bash -c "printf 'mylib_test\n2\n' | '$CUP_BIN' add test"
    [ "$status" -eq 0 ]
    [ -f src/tests/mylib_test.cpp ]
    grep -q '#include "mylib.hpp"' src/tests/mylib_test.cpp

    run "$CUP_BIN" test
    [ "$status" -eq 0 ]
    [[ "$output" == *"mylib_test"* ]]
}

@test "register/unregister apt-install round-trips through third_party.mk (Make)" {
    # register_apt's Make branch skips the CMake-only "find_package name?"
    # prompt: just the apt package name, then whether to install now
    # (declined, so this needs no network/sudo).
    run bash -c "printf '3\nfoopkg\nn\n' | '$CUP_BIN' register"
    [ "$status" -eq 0 ]
    grep -q '# cup-apt: foopkg' third_party/third_party.mk

    run bash -c "printf 'y\n' | '$CUP_BIN' unregister foopkg"
    [ "$status" -eq 0 ]
    ! grep -q '# cup-apt: foopkg' third_party/third_party.mk
}
