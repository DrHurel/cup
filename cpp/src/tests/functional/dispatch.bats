#!/usr/bin/env bats
# Black-box tests of the compiled cup binary's argv dispatch (main.cpp ->
# cup::cmd::run_main): help/no-args/unknown-command/completion output. These
# don't touch a project on disk, so they run standalone, unlike build_run.bats.
#
# $CUP_BIN is set by CTest (see functional/CMakeLists.txt); default it here so
# the file also runs standalone via `bats functional/dispatch.bats` against a
# manually built tree.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"

@test "no arguments prints usage and exits 0" {
    run "$CUP_BIN"
    [ "$status" -eq 0 ]
    [[ "$output" == *"usage: cup <command> [args]"* ]]
    [[ "$output" == *"build"* ]]
}

@test "--help prints usage and exits 0" {
    run "$CUP_BIN" --help
    [ "$status" -eq 0 ]
    [[ "$output" == *"usage: cup <command> [args]"* ]]
}

@test "-h prints usage and exits 0" {
    run "$CUP_BIN" -h
    [ "$status" -eq 0 ]
    [[ "$output" == *"usage: cup <command> [args]"* ]]
}

@test "an unknown command reports the error, prints usage, and exits 1" {
    run "$CUP_BIN" bogus
    [ "$status" -eq 1 ]
    [[ "$output" == *'unknown command "bogus"'* ]]
    [[ "$output" == *"usage: cup <command> [args]"* ]]
}

@test "clean outside a cup project reports the error and exits 1" {
    cd "$BATS_TEST_TMPDIR"
    run "$CUP_BIN" clean
    [ "$status" -eq 1 ]
    [[ "$output" == *"not inside a cup project"* ]]
}

@test "completion bash prints a sourceable bash completion script" {
    run "$CUP_BIN" completion bash
    [ "$status" -eq 0 ]
    [[ "$output" == *"_cup()"* ]]
    [[ "$output" == *"complete -F _cup cup"* ]]
}

@test "completion zsh prints a compdef script" {
    run "$CUP_BIN" completion zsh
    [ "$status" -eq 0 ]
    [[ "$output" == *"#compdef cup"* ]]
}

@test "completion fish prints fish complete directives" {
    run "$CUP_BIN" completion fish
    [ "$status" -eq 0 ]
    [[ "$output" == *"complete -c cup"* ]]
}

@test "completion with an unsupported shell fails with a usage error" {
    run "$CUP_BIN" completion badshell
    [ "$status" -eq 1 ]
    [[ "$output" == *"usage: cup completion"* ]]
}

@test "an EOF on the first interactive prompt reports \"aborted.\" and exits 1" {
    cd "$BATS_TEST_TMPDIR"
    printf 'name = "fixture"\ncup_version = "0.1.0"\ncpp_standard = 17\nstd_module = false\nbuild_tool = "cmake"\n' > cup.toml
    run bash -c "'$CUP_BIN' add app < /dev/null"
    [ "$status" -eq 1 ]
    [[ "$output" == *"aborted."* ]]
}

@test "docker with no args prints a usage error" {
    cd "$BATS_TEST_TMPDIR"
    printf 'name = "fixture"\ncup_version = "0.1.0"\ncpp_standard = 17\nstd_module = false\nbuild_tool = "cmake"\n' > cup.toml
    run "$CUP_BIN" docker
    [ "$status" -eq 1 ]
    [[ "$output" == *"usage: cup docker <new|build|push>"* ]]
}

@test "docker with an unknown subcommand fails with a usage error" {
    cd "$BATS_TEST_TMPDIR"
    printf 'name = "fixture"\ncup_version = "0.1.0"\ncpp_standard = 17\nstd_module = false\nbuild_tool = "cmake"\n' > cup.toml
    run "$CUP_BIN" docker bogus
    [ "$status" -eq 1 ]
    [[ "$output" == *"unknown \`cup docker\` subcommand \"bogus\""* ]]
}

@test "unregister with nothing registered reports it and exits 0" {
    cd "$BATS_TEST_TMPDIR"
    printf 'name = "fixture"\ncup_version = "0.1.0"\ncpp_standard = 17\nstd_module = false\nbuild_tool = "cmake"\n' > cup.toml
    run "$CUP_BIN" unregister
    [ "$status" -eq 0 ]
    [[ "$output" == *"no third-party dependencies registered"* ]]
}
