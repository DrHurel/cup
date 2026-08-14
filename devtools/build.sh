#!/usr/bin/env sh
set -eu
# Bootstrap: no cup exists yet to build itself, so this is plain cmake/ninja
# (see docs/migration-cpp23.md's Phase 5). It targets build/Release directly —
# the same path cup's own `build`/`configure` commands use for that mode — so
# this first build and every self-hosted one after it share one tree instead
# of a throwaway bootstrap copy plus a separate self-hosted one. Once
# build/cup exists, prefer `build/cup build` on the project directly.
cmake -G Ninja -B build/Release -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
cp build/Release/bin/cup build/cup
printf 'Built %s\n' "build/cup"
