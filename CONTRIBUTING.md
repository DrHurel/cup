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

- `src/libs/` — the libraries, one C++20 module per concern, each directly
  under `src/libs` (never in a subfolder: module and namespace names come
  from that path) — `project`, `scaffold`, `tmpl`, `ui`, `cmd`, `logging`, ...
- `src/libs/core/`, `src/libs/posix/`, `src/libs/windows/` — the platform
  layer: the portable `core` module and its two OS backends (only the host's
  is built)
- `src/apps/cup/` — the three-line `main()` shim over `cmd::run_main`
- `src/tests/` — the Catch2 suites, laid out like `src/libs`: the suites of
  the library at `src/libs/<path>/` live in `src/tests/<path>/` (`core/`,
  `posix/`, `cup/cmd/`, ...). Plus `src/tests/functional/` (bats and the pty
  suite, black-box tests against the real compiled binary) and
  `src/tests/support/` (headers shared by several suites)
- `templates/` — the scaffolding template corpus, embedded into the binary
  at build time by `cmake/EmbedTemplates.cmake`
- `docker/ci/` — the images every Linux CI job runs in (Ubuntu + GCC 15/16,
  Alpine, Debian sid, Fedora). CI publishes them to
  `ghcr.io/drhurel/cup-ci` itself (`job-ci-images.yml`), tagged by a hash of
  the Dockerfile. To add a tool a job needs, add it to the Dockerfile rather
  than an install step: the next run builds and pushes the new image before
  any job uses it.
- `docs/migration-cpp23.md` — the full history of the Go→C++23 port; still
  the best source for *why* a given seam or module boundary looks the way it
  does

## Before opening a PR

- `./devtools/test.sh` passes
- New behavior has Catch2 coverage; a new command or user-visible flow gets a
  `src/tests/functional/*.bats` case too
- Every library exports a `Library` class (in its primary interface, e.g.
  `cmd`'s `cmd.cppm`): `dependencies`, a `std::tuple` of the `Library`
  classes of the libraries its CMake target links, and static
  `initialize()` / `finalize()` hooks. `library` (`library.cppm`)
  checks the shape and brings a graph up dependencies-first and down in
  reverse; `run_main` holds a `library::Scope<cmd::Library>` for
  the whole command. A new library gets one, and a library that starts
  linking another adds it to `dependencies`
- OS-specific code lives only in the backend libraries
  (`src/libs/posix/`, `src/libs/windows/`), never
  behind an `#ifdef _WIN32` elsewhere: add the capability to the `Services`
  table (`services.cppm`), implement it in both backends, and wire it in both
  services files (`src/libs/posix/PosixServices.cpp`,
  `src/libs/windows/WindowsServices.cpp`).
  CMake builds one backend and one services file per platform. Test-only
  shims go in `TestCompat.hpp`. `process_test`, `windows_platform_test` and
  the Windows CI job (`job-build-windows.yml`) exercise the Windows side
- No two files in one directory may differ only by case (`Error.cppm` next
  to `error.cppm`): a Windows or macOS checkout keeps just one of them
- SonarCloud's quality gate runs on every PR (new-code coverage ≥ 80%, no new
  issues) — `sonar-project.properties` has the details if a finding needs a
  documented suppression rather than a fix

## Issues and boards

An issue goes through three GitHub Projects, each with its own Status column:

| Board | Columns |
|---|---|
| [PO Refinement](https://github.com/users/DrHurel/projects/10) | Proposition → Refined ticket → Approved / Rejected |
| [Dev](https://github.com/users/DrHurel/projects/11) | Ready to dev → To do → Work in progress ⇄ Blocked / Missing information → Reviewed → Validated → Done / Rejected |
| [QA](https://github.com/users/DrHurel/projects/12) | Release ready → QA in progress → Ready to deploy / Bug |

GitHub only moves a card within its own board. `just boards-sync` (or
Actions → **Boards sync**) moves it from one board to the next:

- PO *Approved* → Dev *Ready to dev*;
- Dev *Missing information* → PO *Refined ticket*;
- Dev *Rejected* → PO *Rejected*;
- QA *Bug* → Dev *Ready to dev*;
- any open issue on no board → PO *Proposition*.

The latest Status change wins, so a second run moves nothing. Dev *Done* → QA
*Release ready* happens **only** when a release is cut (`just release`).
`just boards-sync dry-run` lists the moves without making them. The workflow
reads the `PROJECTS_TOKEN` secret (scopes `project` and `repo`).

## Branches and commits

- One branch per issue, cut from its target (`main`, or `release/X.Y.Z` for a
  release fix) and **always rebased** on it: `git fetch && git rebase
  origin/<target>`. Never merge the target into the branch.
- **Every merged commit carries its tag** (see `CLAUDE.md`): an issue (`#42`),
  a SonarCloud rule key (`cpp:S5272`) or `[no-issue]`.
- Merge **by squash** by default: the PR title becomes the commit message, so
  it follows the same format, e.g. `[#42] fix(ui) : …`.
- A PR covering **several issues**: one commit per issue, each with its tag,
  merged **by rebase**.
- The repository allows squash and rebase merges only. The `commit-msg` hook
  (`just hooks` installs it) refuses a commit without a tag; the **Ticket
  reference** workflow refuses a PR whose title or a commit has none. Do not
  merge a PR where it is red. GitHub appends `(#<PR number>)` to a squash
  commit: that number is not the issue's tag.

## Releasing

Versions are semver `X.Y.Z`; the version is cup.toml's `cup_version`.

1. Bump `cup_version` to `X.Y.0` on `main` (a PR), then **`just release
   X.Y.0`** (or `/release`): it cuts `release/X.Y.0` from `main` and moves the
   Dev *Done* issues to QA *Release ready*. Features are frozen: only bug fixes
   land on that branch, by PR. `just release X.Y.Z` (Z > 0) cuts a fix release
   from the last frozen `vX.Y.*` tag — its first PR bumps `cup_version` to
   `X.Y.Z`. A version already used (tag, branch or GitHub Release) is refused.
2. **Every push on the branch** publishes a release candidate: tag and
   prerelease `vX.Y.Z-rc.N` with the musl-static binary and the .deb, built by
   `.github/workflows/release.yml`. QA tests it.
3. **`just release-freeze X.Y.Z`** (or `/release-freeze`), once QA has
   validated it, tags `vX.Y.Z`, and `release.yml` publishes the final release.
   It is refused while an issue on the QA board is not *Ready to deploy* or
   when the branch's last commit has no release candidate (`--force` to
   override). Then bring the fixes back with a PR `release/X.Y.Z` → `main`.

`release.yml` refuses to build a version that differs from `cup_version`: what
`cup --version` and the .deb report is always the release's version.
