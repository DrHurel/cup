#!/usr/bin/env sh
set -eu
mkdir -p build
go build -o build/cup .
printf 'Built %s\n' "build/cup"
