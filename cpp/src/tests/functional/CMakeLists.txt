# Black-box functional tests: BATS scripts that invoke the real compiled cup
# binary as a subprocess (argv in, stdout/exit code out), the way a user's
# shell would — complementing the Catch2 suites, which link cup's libraries
# directly and stub network/process seams. Skipped, not failed, when bats
# isn't installed, so a plain local build without it still configures and
# builds cleanly; CI installs the `bats` package specifically to run this.
find_program(BATS_EXECUTABLE bats)

if(BATS_EXECUTABLE)
  add_test(NAME functional_dispatch
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/dispatch.bats")
  add_test(NAME functional_build_run
           COMMAND ${BATS_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/build_run.bats")
  set_tests_properties(functional_dispatch functional_build_run PROPERTIES
                       ENVIRONMENT "CUP_BIN=$<TARGET_FILE:cup>")
else()
  message(STATUS "cup: bats not found — functional CLI tests will be skipped "
                 "(install the `bats` package to enable them)")
endif()
