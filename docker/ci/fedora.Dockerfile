# CI image for job-build-fedora.yml (main pushes only). See
# .github/workflows/job-ci-images.yml, which builds and publishes it.
#
# fedora:42 ships GCC 15.2 and CMake 3.31 in its own repos.
FROM fedora:42

RUN dnf install -y gcc-c++ cmake ninja-build git bats libcurl-devel make \
    && dnf clean all \
    && git config --system --add safe.directory '*' \
    && useradd -m -u 1001 runner

ENV CMAKE_GENERATOR=Ninja
