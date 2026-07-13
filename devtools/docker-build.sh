#!/usr/bin/env sh
set -eu
tag="${1:-cup-dev}"
docker build -t "$tag" -f docker/Dockerfile .
printf 'Built image %s\n' "$tag"
printf 'Run: docker run --rm -v "$PWD:/work" %s ./devtools/build.sh\n' "$tag"
