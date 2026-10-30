# Black-box functional tests: BATS scripts that invoke the real compiled cup
# binary as a subprocess (argv in, stdout/exit code out), the way a user's
# shell would — complementing the Catch2 suites, which link cup's libraries
# directly and stub network/process seams. Skipped, not failed, when bats
# isn't installed, so a plain local build without it still configures and
# builds cleanly; CI installs the `bats` package specifically to run this.
find_program(BATS_EXECUTABLE bats)

# Drives the real cup binary through a real pty (fork+exec, not synthetic
# ftxui::Event objects), so it lives with the black-box suites — though as a
# Catch2 suite it isn't one of the functional_* ctest entries.
add_executable(interactive_signal_test interactive_signal_test.cpp)
target_compile_features(interactive_signal_test PRIVATE cxx_std_23)
target_compile_definitions(interactive_signal_test PRIVATE CUP_TEST_BIN="$<TARGET_FILE:cup>")
target_link_libraries(interactive_signal_test PRIVATE test_support Catch2::Catch2WithMain)
add_test(NAME interactive_signal_test COMMAND interactive_signal_test)

if(BATS_EXECUTABLE)
  add_test(NAME functional_dispatch
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/dispatch.bats")
  add_test(NAME functional_build_run
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/build_run.bats")
  add_test(NAME functional_modules_build_run
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/modules_build_run.bats")
  add_test(NAME functional_make_build_run
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/make_build_run.bats")
  add_test(NAME functional_thirdparty
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty.bats")
  add_test(NAME functional_completion
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/completion.bats")
  add_test(NAME functional_init
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/init.bats")
  set_tests_properties(functional_dispatch functional_build_run functional_modules_build_run
                       functional_make_build_run functional_thirdparty functional_completion
                       functional_init
                       PROPERTIES
                       ENVIRONMENT "CUP_BIN=$<TARGET_FILE:cup>")
else()
  message(WARNING "cup: bats not found — functional CLI tests will be skipped "
                  "(install the `bats` package to enable them)")
endif()
