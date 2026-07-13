# cup

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
  cup.toml                 project marker (name, cup version, cpp_standard)
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
