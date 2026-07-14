# cup

[![CI](https://github.com/DrHurel/cup/actions/workflows/ci.yml/badge.svg)](https://github.com/DrHurel/cup/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=coverage)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=reliability_rating)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Security Rating](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=security_rating)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=bugs)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=code_smells)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Vulnerabilities](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=vulnerabilities)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Duplicated Lines (%)](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=duplicated_lines_density)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Technical Debt](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=sqale_index)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=DrHurel_cup&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=DrHurel_cup)


`cup` scaffolds and manages C++ projects from a single Go binary. Pick a
standard when you create a project and `cup` scaffolds to match it: **C++20/23**
projects are built from C++ modules (`import std;` on C++23), while
**C++11/14/17** projects use classic headers. Either way the projects it creates
are *thin* — just source, `CMakeLists.txt`, and a `cup.toml` marker. All build
and scaffolding logic lives in `cup` itself, so one installed binary manages
every project.

## Build & install

```sh
./devtools/build.sh           # produces build/cup
cp build/cup ~/.local/bin/     # put it on PATH (any dir on PATH works)
```

Then enable shell completion (optional):

```sh
cup completion install         # detects your shell and wires it in
```

## Devtools

- `./devtools/build.sh` — build into `build/cup`
- `./devtools/test.sh` — run Go tests
- `./devtools/clean.sh` — remove `build/`
- `./devtools/docker-build.sh` — build the Docker image

## Docker

Build the Docker image from the repo root:

```sh
docker build -t cup:latest -f docker/Dockerfile .
```

## Commands

```
cup new [name]                     create a new C++ project (prompts for name + standard)
cup add [app|lib|test]             scaffold a target (interactive if no arg)
cup configure [MODE]               generate the CMake build system
cup build [MODE]                   configure + compile
cup rebuild [MODE]                 wipe build/ then compile
cup run [MODE] [app] [-- args]     build then run an app
cup test [MODE]                    build then run the test suite (ctest)
cup retest [MODE]                  wipe build/ then run the test suite
cup clean                          remove the build/ directory
cup compiler                       show the project's minimum compiler versions
cup compiler set <gcc|clang> <v>   change a floor (docker-verified before commit)
cup compiler verify                compile the project in the toolchain image
cup register                       register a third-party dependency
cup unregister [name]              remove a third-party dependency
cup template list                  list library-component templates
cup template new [name]            add a project-local template
cup completion <install|bash|zsh|fish>  install or print shell completion
```

`MODE` is `Debug` (default), `Release`, or `Coverage`; each gets its own
`build/<MODE>` tree.

## Layout of a created project

```
myproj/
  cup.toml                 project marker (name, cup version, cpp_standard, [compiler])
  CMakeLists.txt           per-mode build tree, coverage; import std on C++23
  .gitignore
  src/apps/<name>/         executables (one file per app dir)
  src/libs/<name>/         libraries — C++ modules or classic headers per standard
  src/tests/               ctest executables
  third_party/             dependencies (created by cup register)
  .cup/templates/<kind>/   optional project-local template overrides / additions
```

Libraries scaffold differently per standard. On C++20/23 a lib is a module: a
primary interface (`<name>.cppm`) re-exports partition files (one per symbol). On
C++11/14/17 a lib is a header/source pair driven by a `<name>.hpp` aggregator.

## Templates

`cup` ships built-in templates for the component kinds `class`, `interface`,
`enum`, `free-function`, and `templated-class`, plus `app` and `test` — in two
families, `modules` and `headers`, chosen automatically from the project's
standard. A project can add its own kind — or override a built-in — by dropping a
directory of the same shape into `.cup/templates/<kind>/`; `cup template new`
copies a built-in there to start from. A modules library kind needs a
`source.cppm.tmpl` and a `CMakeLists.txt.tmpl`; a headers kind uses
`source.h.tmpl` + `source.cpp.tmpl`. Placeholders use `{{name}}` syntax (`name`,
`filename`, `module`, `symbol`, `namespace`, `module_import`).

## Toolchain requirements

Requirements scale with the standard you pick:

- **C++23** (`import std;`) needs **CMake ≥ 3.30** (the root `CMakeLists.txt`
  pins an experimental-support UUID for CMake 4.4) and a compiler shipping the
  std-module manifest (**GCC 15+**).
- **C++20** named modules need **CMake ≥ 3.28**.
- **C++11/14/17** have no special requirements beyond a conforming compiler.

On an older toolchain `cup build` stops at CMake's version check — scaffolding
still works everywhere.

## Minimum compiler versions

A project can pin a **minimum GCC and/or Clang version** in the `[compiler]`
table of its `cup.toml`, and the generated root `CMakeLists.txt` enforces it: a
build with an older toolchain stops at a clear `FATAL_ERROR` instead of failing
deep in a compile. `cup new` first asks **which** compilers to pin — GCC, Clang,
or both — then, for each, the version. A compiler you don't pin is simply left
out of `cup.toml` and unenforced. The version picker offers the range from the
standard's baseline up to the newest released compiler:

- The **baseline** (lowest version that can build a standard — C++23 needs GCC
  15 for `import std;`) is a small curated map of stable language facts.
- The **ceiling** is discovered live from the GNU gcc release index and the LLVM
  GitHub releases, so the list never goes stale as new compilers ship. The
  result is cached (~/.cache/cup) for a week and falls back to a bundled default
  when offline, so project creation still works with no network.

When a compiler has a single valid version (e.g. GCC on C++23) it is chosen
without prompting.

```toml
[compiler]
gcc = 15
clang = 17
verify_image = "cup-cxx:latest"   # docker image cup builds in to verify a change
```

Change a floor with `cup compiler set`:

```sh
cup compiler                 # show the current floors and verify image
cup compiler set gcc 14      # lower the GCC floor to 14…
cup compiler verify          # …or just check the project still builds
```

A change is **docker-verified before it is committed**: cup writes the new floor
to `cup.toml` and the CMake guard, then compiles the project inside the
`verify_image` (source mounted read-only, build kept inside the container). If
that build fails, the change is **cancelled** — `cup.toml` and `CMakeLists.txt`
are restored byte-for-byte, so a floor can never claim more than what compiles.

Provide the toolchain image via `verify_image` in `cup.toml` or `--image REF`;
`docker/toolchain.Dockerfile` is a starting point. Use `--no-verify` on `set` to
skip the check when you have no image configured.
