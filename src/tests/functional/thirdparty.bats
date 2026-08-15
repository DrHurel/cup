#!/usr/bin/env bats
# Black-box tests of `cup register`/`cup unregister`/`cup template` against a
# real project, same spirit as build_run.bats: real subprocess, real
# third_party/CMakeLists.txt edits, and (for the "unresolvable dependency"
# case) a real cmake configure failure/recovery. Network-independent: the apt
# method's install step is always declined, and the fixture's empty [docker]
# section means `register`'s sync_default_build_image is a no-op.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"
FIXTURE="${BATS_TEST_DIRNAME}/fixture"

setup() {
    PROJECT_DIR="${BATS_TEST_TMPDIR}/project"
    cp -R "$FIXTURE" "$PROJECT_DIR"
    cd "$PROJECT_DIR"
}

# register's method picker is non-interactive-numbered: 1=git-submodule,
# 2=cmake-download, 3=apt-install. register_apt then asks for the
# find_package name, the apt package name (blank accepts the lowercased
# default), and whether to run apt-get now (declined here).
@test "register apt-install records a find_package line without running apt-get" {
    run bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register"
    [ "$status" -eq 0 ]
    grep -q 'find_package(Foo REQUIRED) # cup-apt: foo' third_party/CMakeLists.txt
    grep -q 'add_subdirectory(third_party)' CMakeLists.txt
}

@test "an unresolvable registered dependency breaks the build until unregistered" {
    bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register" >/dev/null

    run "$CUP_BIN" build
    [ "$status" -eq 1 ]
    [[ "$output" == *"Foo"* ]]

    run bash -c "printf 'y\n' | '$CUP_BIN' unregister Foo"
    [ "$status" -eq 0 ]
    ! grep -q 'find_package(Foo' third_party/CMakeLists.txt

    run "$CUP_BIN" build
    [ "$status" -eq 0 ]
}

@test "unregister declines removal when not confirmed" {
    bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register" >/dev/null

    run bash -c "printf 'n\n' | '$CUP_BIN' unregister Foo"
    [ "$status" -eq 0 ]
    grep -q 'find_package(Foo' third_party/CMakeLists.txt
}

@test "unregister an unknown name reports the known dependencies and exits 1" {
    bash -c "printf '3\nFoo\n\nn\n' | '$CUP_BIN' register" >/dev/null

    run "$CUP_BIN" unregister nosuchdep
    [ "$status" -eq 1 ]
    [[ "$output" == *'no registered dependency named "nosuchdep"'* ]]
    [[ "$output" == *"Foo"* ]]
}

@test "template list prints the built-in header component kinds" {
    run "$CUP_BIN" template list
    [ "$status" -eq 0 ]
    [[ "$output" == *"class"* ]]
    [[ "$output" == *"built-in"* ]]
}

@test "template new copies a built-in kind into .cup/templates" {
    # base picker is numbered too: 1=app, 2=class, ... -- pick 2=class. The
    # "new template kind name?" prompt still consumes a stdin line even
    # though this invocation's positional arg overrides its answer.
    run bash -c "printf '2\nignored\n' | '$CUP_BIN' template new myclass"
    [ "$status" -eq 0 ]
    [ -d .cup/templates/myclass ]
    [ -f .cup/templates/myclass/CMakeLists.txt.tmpl ]
}
