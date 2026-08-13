#!/usr/bin/env sh
set -eu
# Bootstrap: no cup exists yet to build itself, so this is plain cmake/ninja
# (see docs/migration-cpp23.md's Phase 5). Once build/cup exists, prefer
# `build/cup build` on cpp/ directly — it drives the same cmake/ninja
# invocation through cup's own config, with the version-tagged build/<mode>
# tree cup's other commands expect.
cmake -G Ninja -B cpp/build -S cpp -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build
mkdir -p build
cp cpp/build/bin/cup build/cup
printf 'Built %s\n' "build/cup"
