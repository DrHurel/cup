# CI image for the musl-static release build (build-static.yml) and its
# unit/functional test jobs. See .github/workflows/job-ci-images.yml, which
# builds and publishes it to ghcr.io.
#
# alpine:3.23, not 3.22: 3.22 only carries GCC 14.2, and the floor is GCC 15.
FROM alpine:3.23

# Real ninja, not alpine's default samurai: samurai has no dyndep support, so
# CMake refuses to scan for modules. ninja-is-really-ninja points `ninja` at
# ninja-build. The *-static packages carry the archives a fully static link of
# libcurl needs; pkgconf resolves them through libcurl.pc's Libs.private (see
# third_party/CMakeLists.txt's CURL_USE_STATIC_LIBS branch).
RUN apk add --no-cache \
        build-base cmake ninja-build ninja-is-really-ninja git pkgconf file bats \
        curl-dev curl-static \
        openssl-libs-static zlib-static nghttp2-static brotli-static \
        zstd-static libpsl-static libunistring-static libidn2-static \
    && git config --system --add safe.directory '*' \
    && adduser -D -u 1001 runner

ENV CMAKE_GENERATOR=Ninja
