# The POSIX backend's suite: the services file wiring and what has no Windows
# counterpart — a real pseudo-terminal (posix_openpt) for raw mode, fork and
# rlimit failure paths. posix_openpt, grantpt, unlockpt and ptsname are in libc
# on glibc and musl alike, so nothing extra links. TestHttpServer.hpp
# (http_get coverage) runs its accept loop on a second thread.
add_executable(platform_test platform_test.cpp)
target_compile_features(platform_test PRIVATE cxx_std_23)
target_link_libraries(platform_test PRIVATE platform library error test_support
                                            Catch2::Catch2WithMain)
add_test(NAME platform_test COMMAND platform_test)
