# The ui suite types into a pseudo-terminal from a second thread — see
# ScopedTerminalStdin on why the keys cannot be queued up front. glibc 2.34 and musl
# both fold the pthread symbols into libc, so this resolves to nothing on the
# toolchains cup builds against; it is named so an older one links rather than
# failing at std::thread's constructor.
find_package(Threads REQUIRED)

add_executable(ui_test ui_test.cpp)
target_compile_features(ui_test PRIVATE cxx_std_23)
# cup scaffolds the target but not its dependencies: the module under test and
# Catch2's bundled main(). platform is named because the suite imports cup.platform
# itself to check the terminal condition select_one branches on — cup.ui imports it
# without re-exporting it.
target_link_libraries(ui_test PRIVATE ui error platform probe Threads::Threads
                                      Catch2::Catch2WithMain)
add_test(NAME ui_test COMMAND ui_test)

add_executable(platform_test platform_test.cpp)
target_compile_features(platform_test PRIVATE cxx_std_23)
# The suite opens a pseudo-terminal to reach raw mode at all. posix_openpt, grantpt,
# unlockpt and ptsname are in libc on glibc and musl alike, so nothing extra links.
# TempDir.hpp is a plain header shared by the suites that touch the filesystem — the
# http cases read a file:// URL out of one.
target_include_directories(platform_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(platform_test PRIVATE platform error Catch2::Catch2WithMain)
add_test(NAME platform_test COMMAND platform_test)

add_executable(utils_test utils_test.cpp)
target_compile_features(utils_test PRIVATE cxx_std_23)
# The pool cases run tasks on real threads and block on real futures, so the suite
# needs the threading support library in its own right — not merely through utils.
target_link_libraries(utils_test PRIVATE utils error Threads::Threads
                                         Catch2::Catch2WithMain)
add_test(NAME utils_test COMMAND utils_test)

add_executable(tmpl_test tmpl_test.cpp)
target_compile_features(tmpl_test PRIVATE cxx_std_23)
# TempDir.hpp is a plain header shared by the suites that touch the filesystem.
target_include_directories(tmpl_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tmpl_test PRIVATE tmpl error Catch2::Catch2WithMain)
add_test(NAME tmpl_test COMMAND tmpl_test)

add_executable(project_test project_test.cpp)
target_compile_features(project_test PRIVATE cxx_std_23)
target_include_directories(project_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The dogfood parity case re-encodes cup's own cup.toml, which the Go cup wrote.
target_compile_definitions(project_test PRIVATE CUP_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(project_test PRIVATE project error Catch2::Catch2WithMain)
add_test(NAME project_test COMMAND project_test)

# One executable per module, as above — but cup.scaffold is seven partitions, so its
# suite is seven translation units rather than one 1,400-line file. Catch2 collects
# the cases from all of them, and ctest still reports the module as a unit.
add_executable(scaffold_test
    scaffold_std_test.cpp
    scaffold_naming_test.cpp
    scaffold_render_test.cpp
    scaffold_cmake_test.cpp
    scaffold_compiler_test.cpp
    scaffold_releases_test.cpp
    scaffold_dockerhub_test.cpp
)
target_compile_features(scaffold_test PRIVATE cxx_std_23)
target_include_directories(scaffold_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The guard-parity case reads the golden trees the *Go* cup wrote. The path is
# passed rather than derived so the case can skip itself once Phase 6 deletes them.
target_compile_definitions(scaffold_test
    PRIVATE CUP_GO_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/../internal/cmd/testdata/golden")
# cup scaffolds the target but not its dependencies. platform is named because the
# suites substitute its http_get to stay off the network; tmpl for the override
# directory the render cases write into; ui for the ScopedInput that answers the
# overwrite prompt. cup.scaffold imports all three without re-exporting them.
target_link_libraries(scaffold_test
    PRIVATE scaffold platform tmpl ui error Catch2::Catch2WithMain)
add_test(NAME scaffold_test COMMAND scaffold_test)
