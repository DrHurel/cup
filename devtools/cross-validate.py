#!/usr/bin/env python3
"""Cross-validation harness for Phase 5 gate 3 (docs/migration-cpp23.md):
run the Go and C++ cup binaries with identical inputs across the golden-cell
matrix, plus the std_module=false `cup add` case TestAddWithoutStdModule
covers on the Go side, and diff the resulting trees.

Usage:
    python3 devtools/cross-validate.py build/cup cpp/build/bin/cup

Requires pexpect (`pip install pexpect`). Drives each binary over a plain
pipe (pexpect.popen_spawn.PopenSpawn, not a pty), so cup's is_tty check
picks the numbered-select fallback rather than the arrow-key TUI, and
prompts are matched by their text rather than by counting lines — the
number of live compiler-picker choices varies with real GCC-release /
Docker Hub data, so it can't be known in advance.
"""
import difflib
import os
import re
import shutil
import sys
import tempfile

import pexpect
from pexpect.popen_spawn import PopenSpawn

CELLS = [
    ("cmake", 23), ("cmake", 20), ("cmake", 17), ("cmake", 14), ("cmake", 11),
    ("make", 17), ("make", 14), ("make", 11),
]

STD_INDEX_CMAKE = {23: 1, 20: 2, 17: 3, 14: 4, 11: 5}
STD_INDEX_MAKE = {17: 1, 14: 2, 11: 3}

CUP_VERSION_RE = re.compile(r'^(\s*cup_version\s*=\s*)"[^"]*"$', re.M)
HASH_RE = re.compile(r'^(\s*hash\s*=\s*)"[^"]*"$', re.M)


def scaffold(binary, root, tool, std, name="proj"):
    tool_idx = "1" if tool == "cmake" else "2"
    std_idx = str((STD_INDEX_CMAKE if tool == "cmake" else STD_INDEX_MAKE)[std])

    # PopenSpawn (plain pipes, no pty): cup's is_tty check sees a non-tty
    # stdin/stdout and uses its numbered-select fallback instead of the
    # arrow-key TUI, matching this driver's line-based prompt/response model.
    child = PopenSpawn([binary, "new", name], cwd=root, timeout=30, encoding="utf-8")
    child.logfile = None

    child.expect("build system\\?")
    child.expect("choice number\\?")
    child.sendline(tool_idx)

    child.expect("c\\+\\+ standard\\?")
    child.expect("choice number\\?")
    child.sendline(std_idx)

    # Zero or more compiler-floor prompts (gcc/clang, individually or
    # combined), each ending its own "choice number?" — accept whatever
    # default each offers, up to a generous cap, until we reach the base
    # image prompt. choose_base_image runs unconditionally regardless of
    # build tool: a Make project still records a default [[docker.image]]
    # in cup.toml (for `cup compiler verify` / a later `cup docker build`)
    # even though it generates no Dockerfile.
    idx = child.expect(["pin a minimum version for which compilers\\?",
                        "base image repository\\?"])
    if idx == 0:
        child.expect("choice number\\?")
        child.sendline("")
        for _ in range(2):
            idx = child.expect(["minimum \\w+ version\\?", "base image repository\\?"])
            if idx == 1:
                break
            child.expect("choice number\\?")
            child.sendline("")
    child.expect("base image repository\\?")
    child.sendline("debian")
    child.expect("choice number\\?")
    child.sendline("")

    child.expect(pexpect.EOF, timeout=60)
    status = child.wait()
    if status != 0:
        raise RuntimeError(f"{binary} new exited {status}\n{child.before}")


def add_flows_without_std_module(binary, root, name="proj"):
    """Mirrors TestAddWithoutStdModule: flip std_module=false by hand (no
    picker sets it yet, same as cpp/cup.toml itself), then run `cup add app`
    and `cup add lib` and let the tree speak for itself."""
    proj_dir = os.path.join(root, name)
    toml_path = os.path.join(proj_dir, "cup.toml")
    with open(toml_path) as fh:
        content = fh.read()
    content = content.replace('build_tool = "cmake"\n', 'build_tool = "cmake"\nstd_module = false\n', 1)
    with open(toml_path, "w") as fh:
        fh.write(content)

    child = PopenSpawn([binary, "add", "app"], cwd=proj_dir, timeout=30, encoding="utf-8")
    child.logfile = None
    child.expect("app name\\?")
    child.sendline("runner")
    child.expect("source filename\\?")
    child.sendline("")
    child.expect(pexpect.EOF, timeout=30)
    status = child.wait()
    if status != 0:
        raise RuntimeError(f"{binary} add app exited {status}\n{child.before}")

    child = PopenSpawn([binary, "add", "lib"], cwd=proj_dir, timeout=30, encoding="utf-8")
    child.logfile = None
    # No existing libs yet, so pick_or_new skips its picker and goes
    # straight to the "new lib name?" text prompt.
    child.expect("new lib name\\?")
    child.sendline("lib_class")
    child.expect("template kind\\?")
    child.expect("choice number\\?")
    child.sendline("1")
    child.expect("primary symbol name\\?")
    child.sendline("")
    child.expect(pexpect.EOF, timeout=30)
    status = child.wait()
    if status != 0:
        raise RuntimeError(f"{binary} add lib exited {status}\n{child.before}")


def snapshot(root):
    """Deterministic text manifest of the scaffolded tree, normalising the
    fields Go's own golden tests already treat as expected-to-vary
    (cup_version, docker image content hash) and skipping .git entirely."""
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            with open(full, "r", errors="replace") as fh:
                content = fh.read()
            content = CUP_VERSION_RE.sub(r'\1"X"', content)
            content = HASH_RE.sub(r'\1"X"', content)
            entries.append((rel, content))
    entries.sort(key=lambda e: e[0])
    return entries


def diff_trees(go_root, cpp_root):
    go_snap = dict(snapshot(go_root))
    cpp_snap = dict(snapshot(cpp_root))
    go_files = set(go_snap)
    cpp_files = set(cpp_snap)
    problems = []
    only_go = go_files - cpp_files
    only_cpp = cpp_files - go_files
    if only_go:
        problems.append(f"files only in Go tree: {sorted(only_go)}")
    if only_cpp:
        problems.append(f"files only in C++ tree: {sorted(only_cpp)}")
    for rel in sorted(go_files & cpp_files):
        if go_snap[rel] != cpp_snap[rel]:
            d = "\n".join(difflib.unified_diff(
                go_snap[rel].splitlines(), cpp_snap[rel].splitlines(),
                fromfile=f"go/{rel}", tofile=f"cpp/{rel}", lineterm=""))
            problems.append(f"content differs: {rel}\n{d}")
    return problems


def main():
    go_bin = os.path.abspath(sys.argv[1])
    cpp_bin = os.path.abspath(sys.argv[2])
    workdir = tempfile.mkdtemp(prefix="cup-xvalidate-")
    print(f"workdir: {workdir}")

    failures = []
    for tool, std in CELLS:
        cell = f"{tool}-cpp{std}"
        go_root = os.path.join(workdir, cell, "go")
        cpp_root = os.path.join(workdir, cell, "cpp")
        os.makedirs(go_root)
        os.makedirs(cpp_root)
        print(f"== {cell} ==", flush=True)
        try:
            scaffold(go_bin, go_root, tool, std)
            scaffold(cpp_bin, cpp_root, tool, std)
        except Exception as e:
            print(f"  SCAFFOLD ERROR: {e}")
            failures.append((cell, [f"scaffold error: {e}"]))
            continue
        problems = diff_trees(os.path.join(go_root, "proj"), os.path.join(cpp_root, "proj"))
        if problems:
            print(f"  DIFF ({len(problems)} problem(s))")
            for p in problems:
                print("   -", p.splitlines()[0])
            failures.append((cell, problems))
        else:
            print("  OK — byte-identical")

    # The matrix above never sets std_module (no picker does) — add the one
    # case that matters outside it: C++23 + std_module=false, mirroring
    # TestAddWithoutStdModule.
    cell = "cmake-cpp23-no-std-module"
    go_root = os.path.join(workdir, cell, "go")
    cpp_root = os.path.join(workdir, cell, "cpp")
    os.makedirs(go_root)
    os.makedirs(cpp_root)
    print(f"== {cell} ==", flush=True)
    try:
        scaffold(go_bin, go_root, "cmake", 23)
        add_flows_without_std_module(go_bin, go_root)
        scaffold(cpp_bin, cpp_root, "cmake", 23)
        add_flows_without_std_module(cpp_bin, cpp_root)
        problems = diff_trees(os.path.join(go_root, "proj"), os.path.join(cpp_root, "proj"))
    except Exception as e:
        problems = [f"error: {e}"]
    if problems:
        print(f"  DIFF ({len(problems)} problem(s))")
        for p in problems:
            print("   -", p.splitlines()[0])
        failures.append((cell, problems))
    else:
        print("  OK — byte-identical")

    total = len(CELLS) + 1  # + the std_module=false case
    print()
    if failures:
        print(f"{len(failures)}/{total} cells differ:")
        for cell, problems in failures:
            print(f"\n=== {cell} ===")
            for p in problems:
                print(p)
        sys.exit(1)
    else:
        print(f"All {total} cells byte-identical (modulo cup_version/hash).")
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
