# Devtools

Helper scripts for building, testing, and packaging the `cup` CLI.

## Usage

- `./devtools/build.sh` — bootstrap-build `cup` (plain `cmake -G Ninja`, since
  there is no `cup` yet to build itself) into `build/cup`
- `./devtools/test.sh` — configure, build and run the full Catch2 suite via
  `ctest` in a separate Debug tree; `build/cup test` does the same thing
  through cup itself once you have a built binary
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
