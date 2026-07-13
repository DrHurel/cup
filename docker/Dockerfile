# Utility image: a portable Go build environment for the cup CLI.
#
# It carries only the toolchain — the project is NOT copied in. Mount your
# working tree at /work and run the devtools scripts (or plain `go`) against it:
#
#   docker build -t cup-dev -f docker/Dockerfile .
#   docker run --rm -v "$PWD:/work" cup-dev ./devtools/build.sh
#   docker run --rm -it -v "$PWD:/work" cup-dev            # drops into a shell
#
FROM golang:1.26-alpine

# git is handy for `go` module fetches; ca-certificates for TLS to proxy.git.
# The safe.directory line lets git run inside the host-owned mount; without it
# git refuses ("dubious ownership") and `go build` VCS stamping fails.
RUN apk add --no-cache git ca-certificates \
    && git config --global --add safe.directory '*'

WORKDIR /work

CMD ["sh"]
