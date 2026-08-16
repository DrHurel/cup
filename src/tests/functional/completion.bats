#!/usr/bin/env bats
# Exercises the generated bash completion script the way a real shell would:
# source it, then call its _cup function directly with COMP_WORDS/COMP_CWORD
# set and check COMPREPLY, the same protocol bash's programmable completion
# uses -- no pty/TTY needed, unlike a real interactive tab-press. dispatch.bats
# already checks that `cup completion bash/zsh/fish` prints *something*
# shell-appropriate; this checks that the bash script actually completes.
: "${CUP_BIN:=${BATS_TEST_DIRNAME}/../../../build/Debug/bin/cup}"

@test "bash completion suggests only subcommands matching the typed prefix" {
    source <("$CUP_BIN" completion bash)
    COMP_WORDS=(cup bu)
    COMP_CWORD=1
    _cup
    [[ " ${COMPREPLY[*]} " == *" build "* ]]
    [[ " ${COMPREPLY[*]} " != *" test "* ]]
    [[ " ${COMPREPLY[*]} " != *" run "* ]]
}

@test "bash completion suggests every MODE after a mode-taking subcommand" {
    source <("$CUP_BIN" completion bash)
    COMP_WORDS=(cup build "")
    COMP_CWORD=2
    _cup
    [[ " ${COMPREPLY[*]} " == *" Debug "* ]]
    [[ " ${COMPREPLY[*]} " == *" Release "* ]]
    [[ " ${COMPREPLY[*]} " == *" Coverage "* ]]
}

@test "bash completion suggests MODE for every mode-taking subcommand, not just build" {
    source <("$CUP_BIN" completion bash)
    for cmd in configure rebuild run test retest; do
        COMP_WORDS=(cup "$cmd" "")
        COMP_CWORD=2
        _cup
        [[ " ${COMPREPLY[*]} " == *" Debug "* ]]
    done
}

@test "bash completion suggests add's categories, not MODE" {
    source <("$CUP_BIN" completion bash)
    COMP_WORDS=(cup add "")
    COMP_CWORD=2
    _cup
    [[ " ${COMPREPLY[*]} " == *" app "* ]]
    [[ " ${COMPREPLY[*]} " == *" lib "* ]]
    [[ " ${COMPREPLY[*]} " != *" Debug "* ]]
}

@test "bash completion suggests compiler's subcommands" {
    source <("$CUP_BIN" completion bash)
    COMP_WORDS=(cup compiler "")
    COMP_CWORD=2
    _cup
    [[ " ${COMPREPLY[*]} " == *" show "* ]]
    [[ " ${COMPREPLY[*]} " == *" set "* ]]
    [[ " ${COMPREPLY[*]} " == *" verify "* ]]
}

@test "bash completion offers no third-level suggestions for a plain command" {
    source <("$CUP_BIN" completion bash)
    COMP_WORDS=(cup clean "")
    COMP_CWORD=2
    COMPREPLY=()
    _cup
    [ "${#COMPREPLY[@]}" -eq 0 ]
}
