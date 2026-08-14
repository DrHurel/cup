# Utility image: a portable C++23-modules build environment for the cup CLI.
#
# It carries only the toolchain — the project is NOT copied in. Mount your
# working tree at /work and run the devtools scripts against it:
#
#   docker build -t cup-dev -f docker/Dockerfile .
#   docker run --rm -v "$PWD:/work" cup-dev ./devtools/build.sh
#   docker run --rm -it -v "$PWD:/work" cup-dev            # drops into a shell
#
# Matches cpp.yml's gcc-15 (floor) CI job's toolchain exactly, so a build that
# works here works there.
FROM ubuntu:24.04

# git is handy for module fetches and cup's own `git init`; ca-certificates
# for TLS. The safe.directory line lets git run inside the host-owned mount;
# without it git refuses ("dubious ownership").
#
# g++-15: noble's own archive tops out at g++-13, and there is no official
# g++-15 package for it yet (earliest is Ubuntu 25.10) — same PPA the CI
# floor job trusts. CC/CXX are pinned so plain `cmake` (as devtools/build.sh
# invokes it) picks GCC 15 without every caller having to export them.
RUN apt-get update \
    && apt-get install -y --no-install-recommends software-properties-common gpg-agent \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        g++-15 ninja-build cmake libcurl4-openssl-dev git ca-certificates \
    && git config --global --add safe.directory '*' \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-15 CXX=g++-15

WORKDIR /work

CMD ["sh"]
