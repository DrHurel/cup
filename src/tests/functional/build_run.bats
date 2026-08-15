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

# Defensive: the permission-denied test chmods build/ unreadable: if an
# assertion fails between the chmod and its restore, bats' `[ ... ]` aborts
# the test immediately and the restore line never runs, which would then
# break BATS_TEST_TMPDIR cleanup.
teardown() {
    [ -d "${PROJECT_DIR}/build" ] && chmod -R u+rwx "${PROJECT_DIR}/build" 2>/dev/null
    true
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

@test "run with multiple apps and no explicit name prompts a numbered picker" {
    bash -c "printf 'alpha\n\n' | '$CUP_BIN' add app" >/dev/null
    bash -c "printf 'beta\n\n' | '$CUP_BIN' add app" >/dev/null

    # resolve_app's picker is numbered on non-interactive stdin; apps() is
    # sorted, so 1=alpha, 2=beta.
    run bash -c "printf '2\n' | '$CUP_BIN' run"
    [ "$status" -eq 0 ]
}

@test "add lib on an existing lib extends it with a nested subfolder lib" {
    bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib" >/dev/null

    # pick_or_new now offers "mylib" (1) / "[new…]" (2) -- pick 1 to extend
    # it. extend_lib's "add to 'mylib' as?" is file(1)/subfolder(2) -- pick
    # 2, then a fresh subfolder name, then the same kind/symbol prompts a
    # brand-new lib gets (blank = class / default symbol).
    run bash -c "printf '1\n2\nnested\n\n\n' | '$CUP_BIN' add lib"
    [ "$status" -eq 0 ]
    [ -f src/libs/mylib/nested/nested.hpp ]
    grep -q "add_subdirectory(nested)" src/libs/mylib/CMakeLists.txt

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    # See the "add lib scaffolds a header-only class lib" test: no
    # CMAKE_ARCHIVE_OUTPUT_DIRECTORY in this plain fixture, so the archive
    # lands under CMake's default per-target dir.
    [ -f build/Debug/src/libs/mylib/nested/libnested.a ]
}

@test "add lib on an existing lib can add a plain file instead of a subfolder" {
    bash -c "printf 'mylib\n\n\n' | '$CUP_BIN' add lib" >/dev/null

    # pick "mylib" (1), "as file" (1), a filename, then blank kind/symbol.
    # The filename is used verbatim (unlike a brand-new lib, where the file
    # is named after the capitalized symbol) -- "extra", not "Extra".
    run bash -c "printf '1\n1\nextra\n\n\n' | '$CUP_BIN' add lib"
    [ "$status" -eq 0 ]
    [ -f src/libs/mylib/extra.h ]
    [ -f src/libs/mylib/extra.cpp ]
    grep -q '#include "extra.h"' src/libs/mylib/mylib.hpp
    grep -q 'extra.cpp' src/libs/mylib/CMakeLists.txt

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
}

@test "a second build with no source changes does no rebuild work" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null
    run "$CUP_BIN" build
    [ "$status" -eq 0 ]

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    [[ "$output" == *"no work to do"* ]]
}

@test "registering the same apt dependency name twice is idempotent" {
    bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register" >/dev/null
    run bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register"
    [ "$status" -eq 0 ]
    [ "$(grep -c 'find_package(Foo REQUIRED)' third_party/CMakeLists.txt)" -eq 1 ]
}

@test "unregister with no name and multiple dependencies prompts a numbered picker" {
    bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register" >/dev/null
    bash -c "printf '3\nBar\n\nn\n' | '$CUP_BIN' register" >/dev/null

    # resolve_dependency's picker is numbered in registration/file order:
    # 1=Foo, 2=Bar.
    run bash -c "printf '1\ny\n' | '$CUP_BIN' unregister"
    [ "$status" -eq 0 ]
    ! grep -q 'find_package(Foo' third_party/CMakeLists.txt
    grep -q 'find_package(Bar' third_party/CMakeLists.txt
}

@test "commands work from a subdirectory of the project, not just the root" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null

    run bash -c "cd src/apps/hello && '$CUP_BIN' build"
    [ "$status" -eq 0 ]
    [ -f build/Debug/bin/hello ]
}

@test "a successful command writes status=ok to XDG_CACHE_HOME/cup/cup.log" {
    export XDG_CACHE_HOME="$BATS_TEST_TMPDIR/cache"
    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    grep -q 'command=clean status=ok duration_ms=' "$XDG_CACHE_HOME/cup/cup.log"
}

@test "with XDG_CACHE_HOME unset, cup.log falls back under \$HOME/.cache" {
    unset XDG_CACHE_HOME
    export HOME="$BATS_TEST_TMPDIR/home"
    mkdir -p "$HOME"
    run "$CUP_BIN" clean
    [ "$status" -eq 0 ]
    [ -f "$HOME/.cache/cup/cup.log" ]
}

@test "an EOF on the second interactive prompt reports \"aborted.\" and exits 1" {
    # First prompt (lib name) answered; EOF then hits the "template kind?"
    # prompt, before create_header_lib_at has written anything.
    run bash -c "printf 'mylib\n' | '$CUP_BIN' add lib"
    [ "$status" -eq 1 ]
    [[ "$output" == *"aborted."* ]]
    [ ! -d src/libs/mylib ]
}

@test "a permission-denied build/ directory reports a clean error, not a crash" {
    mkdir -p build
    chmod 000 build

    run "$CUP_BIN" build
    [ "$status" -eq 1 ]
}

@test "a malformed cup.toml reports a parse error instead of crashing" {
    printf 'this is not [valid toml\n' > cup.toml

    run "$CUP_BIN" build
    [ "$status" -eq 1 ]
    [ -n "$output" ]
}

@test "register cmake-download records a FetchContent block without touching the network" {
    # register's method picker: 1=git-submodule, 2=cmake-download,
    # 3=apt-install. Unlike apt (which only ever appends a find_package
    # line), and unlike git-submodule (which shells out to `git submodule
    # add` immediately, needing a live clone), register_download for CMake
    # just appends a FetchContent block -- the network is only touched later,
    # at `cmake configure` time, which this test deliberately doesn't run.
    run bash -c "printf '2\nsomejson\nhttps://example.invalid/somejson.git\nv1.0.0\n' | '$CUP_BIN' register"
    [ "$status" -eq 0 ]
    grep -q 'FetchContent_Declare' third_party/CMakeLists.txt
    grep -q 'GIT_REPOSITORY https://example.invalid/somejson.git' third_party/CMakeLists.txt
    grep -q 'GIT_TAG v1.0.0' third_party/CMakeLists.txt
    grep -q 'FetchContent_MakeAvailable(somejson)' third_party/CMakeLists.txt
}

@test "register apt-install refreshes a configured default build image's Dockerfile" {
    # sync_default_build_image only ever writes docker/<name>/Dockerfile as
    # plain text -- it never shells out to `docker build`, so this needs no
    # Docker daemon.
    # The fixture's cup.toml already ends with a bare [docker] header, so
    # this only needs to append the image entry, not redeclare the table.
    cat >> cup.toml <<'EOF'
  [[docker.image]]
    name = "build"
    base = "ubuntu:24.04"
    version = 1
    default = true
EOF

    run bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register"
    [ "$status" -eq 0 ]
    [ -f docker/build/Dockerfile ]
    grep -q 'FROM ubuntu:24.04' docker/build/Dockerfile
    grep -q 'foo' docker/build/Dockerfile
}

@test "compiler set succeeds and rewrites the guard when markers are present" {
    # Unlike the marker-less rollback test above, this CMakeLists carries a
    # real >>> cup:compiler-guard >>> block, so the rewrite (and thus the
    # whole --no-verify'd change) succeeds.
    cat >> CMakeLists.txt <<'EOF'

# >>> cup:compiler-guard >>>
# <<< cup:compiler-guard <<<
EOF

    run "$CUP_BIN" compiler set gcc 16 --no-verify
    [ "$status" -eq 0 ]
    grep -q 'gcc = 16' cup.toml
    grep -q 'VERSION_LESS 16' CMakeLists.txt
}

@test "an interrupted build recovers cleanly on the next cup build" {
    bash -c "printf 'hello\n\n' | '$CUP_BIN' add app" >/dev/null
    # A custom target hello depends on, slow enough to reliably interrupt
    # mid-build. Appended after add_subdirectory(src/apps), so the `hello`
    # target already exists by the time CMake reaches it.
    cat >> CMakeLists.txt <<'EOF'

add_custom_target(cup_test_slow_dep COMMAND sleep 5)
add_dependencies(hello cup_test_slow_dep)
EOF

    # setsid gives the build its own process group so the SIGINT below
    # reaches cup *and* the cmake/ninja/sleep subprocess tree it spawned,
    # rather than just cup itself (which would otherwise die and leave an
    # orphaned build running in the background to race the retry below).
    setsid "$CUP_BIN" build &
    build_pid=$!
    sleep 1
    kill -INT -- -"$build_pid"
    # A bare failing command would abort the test here (bats runs under
    # errexit); `||` is one of the constructs errexit doesn't trip on, and
    # $? right at the top of that branch is still wait's exit status.
    interrupted_status=0
    wait "$build_pid" 2>/dev/null || interrupted_status=$?
    [ "$interrupted_status" -ne 0 ]

    # Drop the artificial delay and rebuild for real.
    sed -i '/cup_test_slow_dep/d' CMakeLists.txt
    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
    [ -f build/Debug/bin/hello ]
}
