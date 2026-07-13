# Devtools

Helper scripts for building, testing, and packaging the `cup` CLI.

## Usage

- `./devtools/build.sh` — build the executable into `build/cup`
- `./devtools/test.sh` — run Go tests
- `./devtools/clean.sh` — remove the local `build/` tree
- `./devtools/docker-build.sh [tag]` — build the utility dev image (default tag `cup-dev`) from `docker/Dockerfile`

## Docker utility image

`docker/Dockerfile` is a portable Go build environment. It does **not** copy the
project in — mount your working tree at `/work` and run the scripts against it:

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
