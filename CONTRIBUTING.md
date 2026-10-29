# Contributing to cup

## Getting set up

```sh
git clone https://github.com/DrHurel/cup.git
cd cup
```

`cup` is C++23, built with named modules, which need **GCC 15+** and
**CMake 3.28+ with Ninja** (CMake's Makefile generator has no dyndep support
for modules). Ubuntu 24.04's own archive tops out at `g++-13`, so add the
toolchain PPA first — or use an OS that already ships GCC 15+ (Fedora 42+,
any rolling release):

```sh
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install -y g++-15 ninja-build cmake libcurl4-openssl-dev
```

Then build:

```sh
./devtools/build.sh    # bootstrap: plain cmake, produces build/cup
```

The bootstrap is plain `cmake`/`ninja` because there is no `cup` yet to build
itself — that's a one-time thing. Once `build/cup` exists, `build/cup build`
drives the same cmake/ninja invocation through cup's own config, with the
per-mode `build/<mode>` tree (`Debug`/`Release`/`Coverage`) the rest of cup's
commands expect.

Prefer not to install a toolchain locally? `./devtools/docker-build.sh` builds
a `cup-dev` image with GCC 15 + CMake + Ninja already in it:

```sh
./devtools/docker-build.sh
docker run --rm -v "$PWD:/work" cup-dev ./devtools/build.sh
```

On Windows, use MSYS2's UCRT64 environment (GCC 15+, CMake, Ninja) — see the
README's [Windows](README.md#windows) section for the packages and the
configure line. The bootstrap is the same plain `cmake`/`ninja` build.

## Running the tests

```sh
./devtools/test.sh
```

configures a separate Debug tree and runs the full Catch2 suite via `ctest`.
Once you have a built `build/cup`, `build/cup test` does the same thing
through cup itself — that's the self-hosting loop cup's own CI exercises (see
`docs/migration-cpp23.md`'s Phase 5).

## Where things live

- `src/libs/cup/` — the library, one C++20 module per concern
  (`cup.project`, `cup.scaffold`, `cup.tmpl`, `cup.ui`, `cup.platform`,
  `cup.cmd`)
- `src/apps/cup/` — the three-line `main()` shim over `cup.cmd::run_main`
- `src/tests/` — the Catch2 suites, one file per module/concern, plus
  `src/tests/functional/` (bats, black-box tests against the real compiled
  binary)
- `templates/` — the scaffolding template corpus, embedded into the binary
  at build time by `cmake/EmbedTemplates.cmake`
- `docs/migration-cpp23.md` — the full history of the Go→C++23 port; still
  the best source for *why* a given seam or module boundary looks the way it
  does

## Before opening a PR

- `./devtools/test.sh` passes
- New behavior has Catch2 coverage; a new command or user-visible flow gets a
  `src/tests/functional/*.bats` case too
- OS-specific code lives only in cup.platform's backend libraries
  (`src/libs/cup/platform/posix/`, `src/libs/cup/platform/windows/`), never
  behind an `#ifdef _WIN32` elsewhere: add the capability to the `Services`
  table (`services.cppm`), implement it in both backends, and wire it in both
  services files (`services/PosixServices.cpp`, `services/WindowsServices.cpp`).
  CMake builds one backend and one services file per platform. Test-only
  shims go in `TestCompat.hpp`. `process_test`, `windows_platform_test` and
  the Windows CI job (`job-build-windows.yml`) exercise the Windows side
- No two files in one directory may differ only by case (`Error.cppm` next
  to `error.cppm`): a Windows or macOS checkout keeps just one of them
- SonarCloud's quality gate runs on every PR (new-code coverage ≥ 80%, no new
  issues) — `sonar-project.properties` has the details if a finding needs a
  documented suppression rather than a fix

## Releasing

Push a `vX.Y.Z` tag; `.github/workflows/release.yml` builds the musl-static
binary (via the same `build-static.yml` reusable workflow CI uses on every
push) and attaches it to a GitHub Release.
