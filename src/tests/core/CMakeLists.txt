# process_test runs a real child process (child_helper, below) through
# run_command/capture_command — portable, unlike posix/platform_test's pty and
# fork-failure cases, so it is what exercises the Windows CreateProcess path.
add_executable(child_helper child_helper.cpp)
target_compile_features(child_helper PRIVATE cxx_std_23)

add_executable(process_test process_test.cpp)
target_compile_features(process_test PRIVATE cxx_std_23)
target_compile_definitions(process_test PRIVATE CUP_CHILD_HELPER="$<TARGET_FILE:child_helper>")
target_link_libraries(process_test PRIVATE platform error test_support Catch2::Catch2WithMain)
add_dependencies(process_test child_helper)
add_test(NAME process_test COMMAND process_test)
