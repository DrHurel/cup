# The unit suites mirror src/libs: src/tests/<path>/ holds the suites of the
# library at src/libs/<path>/ (core/, posix/, windows/, cup/cmd/, cup/ui/, ...),
# so a library's tests sit at the same relative path as its sources.
# functional/ holds the black-box suites run against the real cup binary, and
# support/ the plain headers shared by several suites.

# The ui suite types into a pseudo-terminal from a second thread — see
# ScopedTerminalStdin on why the keys cannot be queued up front — and
# TestHttpServer.hpp runs its accept loop on one too. glibc 2.34 and musl
# both fold the pthread symbols into libc, so this resolves to nothing on the
# toolchains cup builds against; it is named so an older one links rather than
# failing at std::thread's constructor.
find_package(Threads REQUIRED)

# support/: TempDir.hpp (scratch directories for the suites that touch the
# filesystem), TestCompat.hpp (setenv/unsetenv and friends on Windows) and
# TestHttpServer.hpp (a loopback HTTP server, on a thread, over Winsock on
# Windows).
add_library(test_support INTERFACE)
target_include_directories(test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(test_support INTERFACE Threads::Threads)
if(WIN32)
  target_link_libraries(test_support INTERFACE ws2_32)
endif()

add_subdirectory(core)
# Each platform backend's own suite, built only where that backend is.
if(WIN32)
  add_subdirectory(windows)
else()
  add_subdirectory(posix)
endif()
add_subdirectory(cup)

# A pty and bats are POSIX-only, so the black-box suites stay off a Windows
# build; the unit suites above are what run there. They need the cup target
# from src/apps, so this must come after it — see the top-level CMakeLists.txt.
if(NOT WIN32)
  add_subdirectory(functional)
endif()
