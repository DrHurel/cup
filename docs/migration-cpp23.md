# Migrating cup from Go to C++23 + modules

## Why

`cup` scaffolds C++20/23 module-based projects but is itself written in Go. That
costs us the contributors we most want — C++ developers — and makes the pitch
hollow. The end state is a `cup` that is *created by cup, built by cup, and
managed by cup*: the strongest possible integration test and the strongest
possible demo.

## End state

| | Target |
|---|---|
| Language | C++23 (`-std=c++23`, no GNU extensions) |
| Structure | Named modules (`.cppm`), **no `import std;` yet** — `cpp_standard = 23` + `std_module = false` |
| Compiler floor | GCC 14 / Clang 18 (with libstdc++ 14) |
| Build | CMake 3.28+, **Ninja only** |
| Release binary | musl-static, built in `alpine:3.22` |
| Tests | Catch2 v3 |
| Deps | toml++, Catch2, libcurl |

### Locked decisions and their reasons

**C++23, not C++20.** `std::expected<T, E>` maps 1:1 onto Go's `(T, error)`,
which makes porting ~4,100 lines of error-returning code mechanical rather than
a redesign. `std::print`/`std::println` map onto the 34 `fmt.Printf`/`Sprintf`
sites.

**Named modules, but not `import std;` yet.** The two are independent. Named
modules need GCC 14; `import std;` needs GCC 15 + CMake 3.30 behind an
experimental UUID gate that changes between CMake releases. GCC 14 keeps
`alpine:3.22` (GCC 14.2) viable as the static build container. Use a global
module fragment for now:

```cpp
module;
#include <filesystem>
#include <expected>
export module cup.scaffold;
```

Flipping to `import std;` later is a mechanical per-file diff — exactly the
C++20→C++23 transformation `scaffold.StdVars` already models.

**Ninja only.** CMake's Makefile generator does not implement the dyndep
scanning protocol modules require, and never will. cup's CMake backend already
passes `-G Ninja` (`internal/cmd/build.go:41`), so this needs no work — but note
the consequence: **cup's own Make backend can never build cup.** We already made
the same call for scaffolded projects (`internal/cmd/new.go:101-104` restricts
Make to the headers family). Say it out loud in the README rather than letting a
contributor discover it.

**musl-static, not glibc-static.** `internal/scaffold/compiler_releases.go`
resolves `ftp.gnu.org` and `api.github.com`. Under glibc, `getaddrinfo` goes
through NSS modules that are `dlopen`'d at runtime, so a `-static` glibc binary
fails DNS on any host whose glibc differs from the build host. musl's resolver
is static-clean. Bonus: the current Go binary is 9.9M; a musl-static C++ build
with the 232K template blob should land at 2–4M.

---

## Bootstrap strategy

The Go `cup` creates and manages the C++ `cup` until the C++ `cup` can build
itself. The handover point is explicit and testable (Phase 5).

```
Go cup ──scaffolds──> cup-cpp/ ──builds──> cup (C++) ──builds──> itself
   │                                                                │
   └────────────── cross-validation harness ────────────────────────┘
                   (both binaries, same inputs, diff the trees)
```

---

## Phase 0 — Prep (before any C++ is written)

**0.1 Land the Make branch.** `11-support-for-make-build-system` has uncommitted
work across 12 files. Porting a moving target is strictly worse. Merge first.

**0.2 Freeze the Go behaviour as a spec.** The 3,816 lines of Go tests are the
migration's acceptance criteria. Before touching anything, add golden-file tests
that capture the *full generated tree* for a matrix of `cup new` invocations
(std × build tool × family) into `testdata/golden/`. `internal/scaffold/cmake_test.go`
(483 lines) already covers most of this shape; extend it to write whole trees.
These goldens are shared by both implementations and are the single most
valuable risk-reduction step in the plan.

**0.3 Tag the Go implementation.** `git tag v0.1.0-go` and push. This is the
fallback if the port stalls.

### Closed gap: C++23 without `import std;`

`scaffold.StdVars` used to emit `import std;` unconditionally for std ≥ 23, so
C++23 + named modules + GCC 14 — our target — was not expressible, and the plan
was to scaffold as C++20 and hand-edit the standard up. That left `cup.toml`
recording a standard the project did not build at, which is a lie in the one file
that is supposed to describe the project, and it aimed `cup add` at the wrong
source shape as soon as the standard was corrected.

So cup.toml now carries the two decisions separately:

```toml
cpp_standard = 23
std_module = false     # named modules, global module fragment, GCC 14 floor
```

`std_module` is read by `project.Config.UsesStdModule` and honoured by `cup add`
(source shape) and `cup new` (the CMake floor and the `CMAKE_CXX_MODULE_STD`
opt-in). Unset, it follows the standard, so nothing else changes: C++23 still
means `import std;` and C++20 still means a global module fragment. `cup new` has
no picker for it yet — `cpp/cup.toml` sets it by hand — and `scaffold.MinCompilers`
still maps 23 → GCC 15, which is only a *default*: cpp/ pins GCC 14 / Clang 18 in
`[compiler]`, and pinned floors win.

Still open: a `cup embed <dir>` command. The CMake codegen that replaces
`//go:embed` is hand-written in Phase 1.3 and is a feature other C++ CLI projects
would want.

---

## Phase 1 — Scaffold with the Go cup

**1.1 Create the project.**

```sh
cup new cup --std 20 --build-tool cmake     # modules family, global module fragment
```

C++20 is deliberate: it yields the modules template family with the global
module fragment shape we want, rather than `import std;`.

**1.2 Raise the language, keep the shape.** Record both decisions in `cup.toml`
— `cpp_standard = 23` and `std_module = false` — and set the standard in the root
`CMakeLists.txt` (`set(CMAKE_CXX_STANDARD 23)`, which is what cup would now
render). Then set the floor through cup:

```sh
cup compiler set gcc 14
cup compiler set clang 18
```

Both are inside `CompilerChoices(20, …)` (baseline GCC 11 / Clang 16), so cup
accepts them and rewrites the guard block in place.

**1.3 Write the template-embedding codegen.** `cmake/EmbedTemplates.cmake`
globs `templates/**` and generates a `.cpp` of `std::string_view` constants plus
a lookup table, replacing `//go:embed all:files`. This is the one piece of build
machinery cup cannot scaffold for us. Keep the on-disk layout identical to
`internal/tmpl/files/` so the templates themselves port unchanged.

**1.4 Register dependencies through cup.** Real dogfooding of
`internal/cmd/thirdparty.go`:

```sh
cup register    # toml++     -> FetchContent
cup register    # Catch2 v3  -> FetchContent
cup register    # libcurl    -> apt (libcurl4-openssl-dev)
```

**1.5 Set up CI early**, before there is anything to test:

- `ubuntu-24.04` + `apt install g++-14` — dev build, tests, Sonar coverage (gcov/lcov)
- `ubuntu-24.04` + GCC 16 — catch module bugs on a modern compiler
- `alpine:3.22` — musl-static release artifact

Running GCC 14 (the floor) *and* GCC 16 matters: GCC's module implementation has
sharper edges at 14, and you want ICEs and stale-BMI failures to surface in CI
rather than in a contributor's first PR.

---

## Phase 2 — Port the leaves

The Go package graph is acyclic and gives the port order directly:

```
main ─> cmd ─> project
             ─> scaffold ─> tmpl
                          ─> ui
```

Port leaves first. For each unit: **port its Go tests to Catch2 before porting
the implementation.** They already pass, so they define correct behaviour
precisely.

### 2.1 `cup.ui` — 241 lines + 258 test lines ✅

Modules: `cup.ui` with partitions `:color`, `:prompt`, `:select`.

This carries the terminal platform seam. Isolate it behind a tiny
`cup.platform` module so nothing else learns about termios:

```cpp
export module cup.platform;
export bool is_tty(int fd);
export std::expected<RawMode, Error> enter_raw_mode(int fd);   // tcgetattr/tcsetattr
```

`internal/ui/select.go:40-47` decodes arrow keys as the 3-byte `ESC [ A`
sequence. That logic ports byte-for-byte.

### 2.2 `cup.tmpl` — 149 lines + 129 test lines ✅

Modules: `cup.tmpl` with partitions `:corpus` and `:resolve`.

The `embed.FS` API surface (`ReadFile`, `ReadDir`, `Open`) becomes lookups into
the generated table from Phase 1.3. Keep the same function signatures so
`cup.scaffold` ports without changes.

The split follows the one seam that matters: `:corpus` reads the binary's
embedded copy and touches no disk, `:resolve` layers a project's
`.cup/templates/` overrides over it. `ReadDir` has no equivalent in the generated
table, so directory listings are derived by prefix from the sorted entry list —
a "directory" is any path segment followed by a separator. `std::set` supplies
both the dedupe and the trailing `sort.Strings`.

### 2.3 `cup.project` — 191 lines + 200 test lines ✅

toml++ replaces BurntSushi/toml. `Config`, `DockerConfig`, `DockerImage` become
plain structs with explicit `to_toml`/`from_toml`. Watch round-tripping: cup
rewrites `cup.toml` in place, so comment and ordering preservation matters more
than it looks.

Two fields are **tri-state, not bool/int**, and the distinction is load-bearing:
`[compiler]`'s `gcc`/`clang` (`*int` — unset means "no floor", not zero) and
`std_module` (`*bool` — unset means "follow the standard", which is not the same
as `false`). In C++ they are `std::optional<int>` / `std::optional<bool>`, and
`to_toml` must **omit** an empty one rather than write a default. Getting this
wrong is silent: an `std_module = false` dropped on rewrite flips cup's own
project onto `import std;`, and cup rewrites `cup.toml` on every
`cup compiler set`. `Config::uses_std_module()` carries the fallback
(`std_module` when set, else `standard() >= 23`) — port it as a method, not as an
`if` at each call site; there are three.

Round-tripping turned out to need a spec, not care. `to_toml` is hand-written
rather than delegated to toml++'s serialiser, because the target is not
"valid TOML" but *the exact bytes BurntSushi/toml emits* — Phase 5 diffs trees
from both binaries, and `cup.toml` is in every one. Three of that encoder's rules
are load-bearing, and the one that bites is not the documented one:

- Zero **ints** are still written (`cpp_standard = 0`, `version = 0`) despite
  `omitempty`. Its notion of "empty" covers empty strings, `false` bools and
  empty tables — not numeric zero.
- Empty strings, `false` bools and wholly-empty sub-tables *are* omitted.
- A sub-table header is preceded by a blank line, contents indent two spaces per
  level, so `[[docker.image]]` sits at two and its keys at four.

Those were captured by running the Go encoder over a matrix of configs rather
than read off the struct tags, and they are pinned by parity tests holding the
literal expected bytes. The strongest of them re-encodes cup's own `cpp/cup.toml`
— written by the Go cup — and requires byte equality, which also covers the
`std_module = false` round trip on real data.

**Milestone reached.** `cup.ui`, `cup.tmpl` and `cup.project` build as modules on
the GCC 14 floor and their Catch2 suites pass (`ui_test`, `tmpl_test`,
`project_test`; Debug and Coverage). GCC 14 modules are workable — with two
constraints that cost real time to find, both now documented in the source and
carried into Phase 3 below.

### The two GCC 14 constraints

Both cost a day between them and both will recur, so they are rules for the rest
of the port, not anecdotes.

**1. Two partitions of one module cannot both drag in the heavy standard
library.** `ui.cppm` states this as "at most one partition may use `<print>`,
`<format>` or `<iostream>`". Phase 2 widened it: `cup.project` failed the same
way with `<filesystem>` + `<algorithm>` in `:config` and toml++ in `:io` —
neither partition mentioning any of those three headers.

```
cup.project:io: error: failed to read compiled module cluster N: Bad file data
fatal error: failed to load pendings for 'std::_Mutex_base'
```

It is not those headers specifically; it is any two partitions reaching the
shared `<memory>`/`<mutex>` machinery underneath them. The failure always lands
on the *consumer*, never the partition that caused it. So: **one partition owns
the heavy headers, the rest stay on `<string>`, `<string_view>`, `<vector>`,
`<optional>`, `<expected>`.** In `cup.project` that meant hand-rolling two
lookups to drop `<algorithm>` and moving `Project` — the only path-shaped type —
out of `:config` into `:io`.

**2. A large third-party header in *any* interface unit's global module fragment
can ICE the compiler.** With `#include <toml++/toml.hpp>` in `:io`'s fragment,
GCC 14 segfaults while the primary merges the partition's BMI:

```
In destructor 'toml::v3::impl::utf8_reader_interface::~utf8_reader_interface()':
internal compiler error: Segmentation fault   (maybe_clone_body)
```

The fix generalises, and the repo had already reached for it once:
**put the dependency in a module *implementation* unit** (`module cup.project;`,
no `export`) whose global module fragment never reaches any BMI, and leave only
declarations in the interface partition. `cpp/src/libs/cup/project/Toml.cpp` is
the worked example; `GenerateEmbeddedTemplates.cmake` made the same call for the
template corpus for the same reason. This is strictly better design anyway —
consumers of `cup.project` never pay for the parser — so prefer it from the start
rather than after an ICE.

A corollary for test authors: a module re-exports no declaration from its global
module fragment, so a consumer naming `std::expected<void, E>` must
`#include <expected>` itself. Same rule as the `<functional>` note in
`ui_test.cpp`.

---

## Phase 3 — `cup.scaffold` (984 lines + 1,236 test lines)

The pure-logic core and the highest-value port: almost no platform surface, the
densest test coverage, and it is what the golden files from Phase 0.2 exercise.

Partitions: `:cmake`, `:compiler`, `:releases`, `:naming`, `:render`, `:std`,
`:dockerhub`.

Two notes:

- **`:releases`** holds the second platform seam. `net/http` → libcurl behind
  `cup.platform`'s `http_get`. The design already degrades gracefully — the disk
  cache plus `gccNewestFallback`/`clangNewestFallback`
  (`internal/scaffold/compiler_releases.go:22-24`) mean a failed fetch only
  narrows the picker ceiling. If static TLS in the musl container turns painful,
  shipping without libcurl is a supported degradation, not a regression.
  **Put libcurl in an implementation unit from the start** — it is the same shape
  as toml++ in Phase 2.3, and constraint 2 above says how that ends otherwise.
- **`:releases` concurrency**: the two goroutines + `WaitGroup` at
  `compiler_releases.go:64-67` become two `std::async` + `.get()`.
- **`:std`** is where cup's own configuration lives, so port it faithfully:
  `std_vars(std, std_module)` takes the std-module decision as an argument rather
  than deriving it from the standard, and has *three* module-family cases — C++23
  with `import std;`, C++23 without it (a `module;` + `#include <print>` prelude,
  still `std::println`), and C++20 (`<iostream>` prelude, `std::cout`). The middle
  one is what builds cup itself; if it regresses, `cup add` starts writing sources
  cup's own GCC 14 floor cannot compile.

**Milestone:** the C++ `cup.scaffold` reproduces every golden tree from Phase
0.2 byte for byte.

---

## Phase 4 — `cup.cmd` (2,510 lines + 1,798 test lines)

The largest phase. Port **command by command**, not file by file, keeping the
Go binary shippable throughout. Suggested order — cheapest and most-tested
first, so the machinery is proven before the hard ones:

1. `clean`, `run`, `configure`, `build`, `rebuild`, `test`, `retest` (`build.go`, 164 lines)
2. `new` (307) + `add` / `add_headers` (568)
3. `template` (84), `completion` (240)
4. `compiler` (270) — depends on `scaffold:compiler` and `:releases`
5. `docker` (315), `register` / `unregister` (457) — heaviest use of `runCommand`

`runCommand` (`internal/cmd/util.go:28`) is the third and last platform seam,
and it is the best-positioned code in the repo for porting: **one call site**,
already behind a stubbable indirection for tests. Port it to `cup.platform`:

```cpp
export module cup.platform;
export std::expected<void, Error> run_command(
    const std::filesystem::path& dir, std::string_view name,
    std::span<const std::string> args);          // posix_spawn + waitpid
```

Keep the test seam — Catch2 tests replace it exactly as the Go tests do.

---

## Phase 5 — The relay

Do not switch on vibes. The handover is gated on four checks:

1. **Self-scaffold**: the C++ `cup` runs `cup new` / `cup add` on a scratch
   project and produces trees byte-identical to the Go cup's.
2. **Self-build**: the C++ `cup` runs `cup configure && cup build && cup test`
   on cup's own source tree, green.
3. **Cross-validation**: a harness runs both binaries across the full
   `cup new` matrix (std × build tool × family) and diffs the results. Zero
   diffs. The matrix does not reach `std_module` — no picker sets it — so add one
   case outside it: scaffold C++23, set `std_module = false` in `cup.toml`, run
   the `cup add` flows, diff. That is cup's own configuration, so both binaries
   must agree on it before the handover
   (`TestAddWithoutStdModule` in `internal/cmd/add_test.go` is the Go side).
4. **Coverage parity**: the Catch2 suite covers at least what the Go suite did
   (Sonar gate stays green).

Then:

```sh
# devtools/build.sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release   # bootstrap: plain cmake
cmake --build build
# thereafter: ./build/cup build
```

Bootstrapping honesty: the *first* C++ `cup` must be built by plain
`cmake -G Ninja`, because there is no cup yet to build it. Document that as the
from-source path; everyone else gets the static release binary.

---

## Phase 6 — Retire Go

1. Move Go sources to a `go-legacy` branch; delete from `main`.
2. Rewrite `README.md`: new build instructions, the GCC 14 / CMake 3.28 / Ninja
   floor, the `apt install g++-14` line for Ubuntu 24.04, and the honest note
   that cup's Make backend cannot build cup.
3. Rewrite `sonar-project.properties` for C++ (`sonar.cfamily.*`, gcov reports).
4. Add `CONTRIBUTING.md` — the point of the whole exercise. Lead with: *clone,
   `apt install g++-14 ninja-build cmake`, build.*
5. Delete `go.mod`, `go.sum`, `coverage.out`.

---

## Scope

| Phase | Non-test | Test | Notes |
|---|---:|---:|---|
| 0 Prep | — | +goldens | Land Make branch, freeze spec |
| 1 Scaffold | ~150 | — | CMake + codegen + CI |
| 2 Leaves | 581 | 587 | ui, tmpl, project — ✅ done |
| 3 scaffold | 984 | 1,236 | pure logic, highest value |
| 4 cmd | 2,510 | 1,798 | largest, incremental |
| 5 Relay | — | +harness | Gated on 4 checks |
| 6 Retire | — | — | Docs, CI, cleanup |
| **Total** | **~4,225** | **~3,621** | from 7,943 lines of Go |

Expect the C++ to land at roughly 1.5× the Go line count, plus build files.

## Risks

**GCC 14 module bugs.** The real one — and now largely *characterised* rather
than merely feared. Phase 2 hit both failure modes (BMI merge failure, and an ICE
on a large header in an interface unit's global module fragment), and both have
mechanical fixes that are documented above and cost nothing to apply up front:
keep the heavy headers in one partition, and keep third-party libraries in
implementation units. The fallback to headers with the C++23 decision intact
remains available but no longer looks necessary.

**`cup.cmd` is 60% of the port.** Mitigation: strictly command-by-command, with
the Go binary shippable at every commit.

**Sonar/coverage rework.** `coverage.out` → gcov/lcov is a known quantity but
easy to leave until it blocks the merge. Do it in Phase 1.5, not Phase 6.

**Scope creep into new features.** Of the two identified gaps, only
C++23-without-`import std;` landed in the Go cup, and only because the alternative
was a `cup.toml` that misreported the project's own standard (one field, one
predicate, no new pickers). `cup embed` stays post-relay. The migration ships
behaviour parity, nothing more.
