#!/usr/bin/env sh
set -eu
# A self-contained Debug configure+build+test, independent of build.sh's
# Release tree — see docs/migration-cpp23.md's Phase 5 self-build gate.
cmake -G Ninja -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
ctest --test-dir build/Debug --output-on-failure
