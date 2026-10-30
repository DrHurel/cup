# CI image for every Linux job that runs on the ubuntu-24.04 floor: build,
# unit/functional tests, coverage, SonarCloud and the .deb package (see
# .github/workflows/job-ci-images.yml, which builds and publishes it to
# ghcr.io, and CONTRIBUTING.md's "Where things live"). Built once per GCC_VERSION (15 = the
# floor, 16 = the canary) instead of every job re-adding the toolchain PPA
# and re-installing the same packages on each run.
#
# g++-${GCC_VERSION}: noble's own archive tops out at g++-13, so it comes from
# the ubuntu-toolchain-r PPA, as in docker/Dockerfile.
FROM ubuntu:24.04

ARG GCC_VERSION=15

# What each package is for:
#   g++-N ninja-build cmake libcurl4-openssl-dev  build cup
#   bats make git                                  functional suites (git init, Make backend)
#   lcov gcovr                                     job-coverage.yml / job-sonarcloud.yml
#   dpkg-dev file                                  job-build-deb.yml (cpack's dpkg-shlibdeps)
#   curl unzip gnupg jq                            SonarCloud scanner download + issue report
#   zstd                                           actions/cache's compressor
RUN apt-get update \
    && apt-get install -y --no-install-recommends software-properties-common gpg-agent \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        g++-${GCC_VERSION} ninja-build cmake libcurl4-openssl-dev \
        bats make git ca-certificates \
        lcov gcovr \
        dpkg-dev file \
        curl unzip gnupg jq \
        zstd \
    && apt-get purge -y --auto-remove software-properties-common gpg-agent \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*' \
    && useradd -m -u 1001 runner

# Image-wide, so the nested cmake configures the functional suites run
# through `cup build` pick GCC N too, not noble's system GCC 13.
ENV CC=gcc-${GCC_VERSION} \
    CXX=g++-${GCC_VERSION} \
    GCOV=gcov-${GCC_VERSION} \
    CMAKE_GENERATOR=Ninja
