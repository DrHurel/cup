# Devtools

Helper scripts for building, testing, and packaging the `cup` CLI.

## Usage

- `./devtools/build.sh` — bootstrap-build the C++ `cup` (plain `cmake -G Ninja`
  against `cpp/`, since there is no `cup` yet to build itself) into `build/cup`
- `./devtools/test.sh` — run the Go test suite (the Go sources aren't retired
  until Phase 6 of `docs/migration-cpp23.md`); the C++ suite runs via `ctest`
  or `build/cup test` once you have a built binary
- `./devtools/clean.sh` — remove the local `build/` tree
- `./devtools/docker-build.sh [tag]` — build the utility dev image (default tag `cup-dev`) from `docker/Dockerfile`

## Docker utility image

`docker/Dockerfile` is a portable C++23-modules build environment (GCC 15 +
CMake + Ninja, matching CI's floor job). It does **not** copy the project
in — mount your working tree at `/work` and run the scripts against it:

```sh
./devtools/docker-build.sh
docker run --rm -v "$PWD:/work" cup-dev ./devtools/build.sh
docker run --rm -it -v "$PWD:/work" cup-dev            # interactive shell
```

## Built artifact

The local executable is placed in:

```sh
build/cup
```
