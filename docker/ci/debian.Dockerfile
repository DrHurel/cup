# CI image for job-build-debian.yml (main pushes only). See
# .github/workflows/job-ci-images.yml, which builds and publishes it.
#
# debian:sid, not stable/testing: bookworm ships GCC 12 and trixie GCC 14,
# both under the GCC 15 floor. make is not pulled in by g++ here, and
# make_build_run.bats needs it.
FROM debian:sid

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++-15 ninja-build cmake libcurl4-openssl-dev bats git ca-certificates make \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*' \
    && useradd -m -u 1001 runner

# sid has no unversioned gcc/cc once only g++-15 is installed: the nested
# cmake configures in the functional suites need CC/CXX to find a compiler.
ENV CC=gcc-15 \
    CXX=g++-15 \
    CMAKE_GENERATOR=Ninja
