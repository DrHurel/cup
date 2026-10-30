# Script mode (cmake -P), run by the cup_version_header target on every build:
# writes cup/version.h from version.h.in with the commit the binary is being
# built from, so `cup --version` never reports a stale SHA after a new commit
# or a checkout without a reconfigure.
#
# Inputs (-D):
#   SOURCE_DIR     the cup checkout, asked for its HEAD commit
#   TEMPLATE       cmake/version.h.in
#   OUTPUT         the generated header
#   CUP_VERSION    cup.toml's cup_version
#   CUP_BUILD_SHA  an explicit SHA (CI passes ${GITHUB_SHA::7}); wins over git
#
# Without an explicit SHA, git is asked for HEAD's short SHA. Outside a git
# checkout (a source tarball) or without git on PATH it stays empty, and
# `cup --version` prints the bare version.
#
# configure_file only rewrites OUTPUT when its content changes, so an
# unchanged commit recompiles nothing.

if(CUP_BUILD_SHA STREQUAL "")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=7 HEAD
      OUTPUT_VARIABLE CUP_BUILD_SHA
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _cup_git_result)
    if(NOT _cup_git_result EQUAL 0)
      set(CUP_BUILD_SHA "")
    endif()
  endif()
endif()

configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)
