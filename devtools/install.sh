#!/usr/bin/env sh
set -eu
# Rebuilds (incremental once build/Release exists — see build.sh) and copies
# the result onto PATH, so `cup` on the command line always reflects the
# latest local build. Destination defaults to ~/.local/bin, the one
# README.md's own install instructions already use; override with a first
# argument for anywhere else on PATH.
dest="${1:-$HOME/.local/bin}"

./devtools/build.sh

mkdir -p "$dest"
cp build/cup "$dest/cup"
chmod +x "$dest/cup"
printf 'Installed %s\n' "$dest/cup"
case ":$PATH:" in
    *":$dest:"*) ;;
    *) printf 'Warning: %s is not on PATH\n' "$dest" >&2 ;;
esac
