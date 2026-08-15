---
name: cup-cli
description: Use whenever working in this repo (or any project scaffolded by it) and the task involves creating, building, testing, running, or scaffolding a C++ project — "cup new", "cup add", "cup build", "cup test", "cup run", "cup configure", "add a lib/app/test", "register a dependency", "docker build image", "compiler floor/verify", or any `cup <subcommand>` invocation. Covers the cup CLI's command set, MODE semantics, and this repo's self-hosted build quirks.
---

# cup CLI

`cup` is a single self-contained binary that scaffolds and manages C++ projects
(C++11 through C++23). This repo (`cup` itself) is *also* a cup-managed
project — its `cup.toml` sets `cpp_standard = 23`, `std_module = false`,
`build_tool = "cmake"`.

Full reference: `README.md` at the repo root. This skill is the quick-action
cheat sheet — read the README for narrative detail (Docker images, compiler
floors, templates) when a task needs more than the command itself.

## Command reference

```
cup new [name]                     create a new C++ project (prompts for name + standard)
cup add [app|lib|test]             scaffold a target (interactive if no arg)
cup configure [MODE]               generate the CMake/Make build system
cup build [MODE]                   configure + compile
cup rebuild [MODE]                 wipe build/ then compile
cup run [MODE] [app] [-- args]     build then run an app
cup test [MODE]                    build then run the test suite (ctest)
cup retest [MODE]                  wipe build/ then run the test suite
cup clean                          remove the build/ directory
cup compiler                       show the project's minimum compiler versions
cup compiler set <gcc|clang> <v>   change a floor (docker-verified before commit)
cup compiler verify                compile the project in the build image
cup docker new                     scaffold a new build image (prompts name + base)
cup docker build [name]            build image(s); bumps the version on a change
cup docker push [name]             push image(s) to the configured registry
cup register                       register a third-party dependency
cup unregister [name]              remove a third-party dependency
cup template list                  list library-component templates
cup template new [name]            add a project-local template
cup completion <install|bash|zsh|fish>  install or print shell completion
cup --version, -v                  print cup's version (and build SHA, for a GitHub-built binary)
```

`MODE` is `Debug` (default), `Release`, or `Coverage` — each gets its own
`build/<MODE>` tree. Most commands are non-interactive when given enough args
(useful for scripted/agent use); omit args to fall into interactive prompts.

## Typical workflows

- **New project**: `cup new myproj` → prompts for standard + build tool →
  `cd myproj && cup add app` / `cup add lib` / `cup add test` to scaffold
  targets → `cup build && cup test`.
- **Iterate**: `cup build [MODE]` after edits; `cup run [MODE] <app> -- args`
  to execute; `cup test [MODE]` to run ctest. Use `rebuild`/`retest` only when
  a clean build is actually needed (stale CMake cache, toolchain change) —
  they wipe `build/` first and cost a full recompile.
- **Add a dependency**: `cup register` (interactive: name, source — apt,
  cmake-download, or submodule); `cup unregister <name>` to remove.
- **Docker build image**: `cup docker new` to add one, `cup docker build` to
  build/version, `cup docker push` to publish. The default image auto-updates
  its Dockerfile when apt deps change via `register`/`unregister`.
- **Compiler floor**: `cup compiler` to inspect, `cup compiler set gcc 14` to
  lower/raise a floor (docker-verifies the change and reverts on failure
  unless `--no-verify`), `cup compiler verify` to just check the current floor
  still builds in the image.
- **Custom templates**: `cup template list` to see built-ins, `cup template
  new <kind>` to copy one into `.cup/templates/<kind>/` for local overrides.

## This repo specifically (cup building cup)

`cup` is already installed on PATH (`~/.local/bin/cup`) in this environment —
always drive this repo's own build/test/run tasks through the plain `cup`
command (`cup build`, `cup test`, `cup run`, `cup rebuild`, `cup clean`, …)
from the repo root, since this repo carries a normal `cup.toml`. Don't
manually probe toolchain versions (`g++ --version`, `cmake --version`, …) or
reach for `./devtools/*.sh` first — `cup` handles configure+build itself and
reports any toolchain problem directly.

The `devtools/*.sh` scripts exist only as the bootstrap path for a machine
that has no `cup` binary at all yet (fresh clone, no PATH install):

```sh
./devtools/build.sh     # bootstrap-builds build/cup via cmake+ninja directly
./devtools/test.sh      # configure, build, and run the Catch2 suite via ctest
./devtools/install.sh   # build.sh, then copies build/cup onto PATH as `cup`
./devtools/clean.sh     # remove build/
```

Reach for them only if `cup` is confirmed missing from PATH — otherwise
default to `cup <subcommand>` for every build/test/run/clean task here.

The one exception: after any successful `cup build`/`cup rebuild` of this
repo itself, run `./devtools/install.sh` — it does an incremental rebuild and
copies the fresh `build/cup` onto PATH, so the `cup` this skill invokes for
every other task actually reflects the latest source instead of a stale
install. Skip it for builds of *other* cup-scaffolded projects, only this repo
self-hosts.

Periodically prefer `cup rebuild [MODE]` over `cup build [MODE]` even without
a toolchain change — it wipes `build/<MODE>` first, clearing stale/orphaned
object files that a plain incremental build can leave behind. Don't do this
every iteration (it costs a full recompile); use it every so often, or when
something looks off after a normal build.

Toolchain floor for this repo: **GCC 15+, CMake ≥ 3.30, Ninja** (see
`[compiler]` in `cup.toml` and README's [Toolchain requirements] section) —
Ubuntu 24.04's default `g++-13` is too old; needs the toolchain PPA or GCC 15+
from elsewhere.
