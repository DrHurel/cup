# The Windows backend's suite: the services file wiring (exe suffix, default
# shell, home/cache fallbacks) and what has no POSIX counterpart (argument
# quoting for CreateProcessW).
add_executable(windows_platform_test windows_platform_test.cpp)
target_compile_features(windows_platform_test PRIVATE cxx_std_23)
target_link_libraries(windows_platform_test PRIVATE platform library error test_support
                                                    Catch2::Catch2WithMain)
add_test(NAME windows_platform_test COMMAND windows_platform_test)
