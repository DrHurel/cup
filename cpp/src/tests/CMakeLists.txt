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
# TestHttpServer.hpp (http_get coverage) runs its accept loop on a second thread.
target_include_directories(platform_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(platform_test PRIVATE platform error Threads::Threads
                                            Catch2::Catch2WithMain)
add_test(NAME platform_test COMMAND platform_test)

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

add_executable(naming_test naming_test.cpp)
target_compile_features(naming_test PRIVATE cxx_std_23)
target_link_libraries(naming_test PRIVATE scaffold error Catch2::Catch2WithMain)
add_test(NAME naming_test COMMAND naming_test)

add_executable(std_test std_test.cpp)
target_compile_features(std_test PRIVATE cxx_std_23)
target_link_libraries(std_test PRIVATE scaffold error Catch2::Catch2WithMain)
add_test(NAME std_test COMMAND std_test)

add_executable(render_test render_test.cpp)
target_compile_features(render_test PRIVATE cxx_std_23)
target_include_directories(render_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(render_test PRIVATE scaffold tmpl error Catch2::Catch2WithMain)
add_test(NAME render_test COMMAND render_test)

add_executable(cmake_test cmake_test.cpp)
target_compile_features(cmake_test PRIVATE cxx_std_23)
target_include_directories(cmake_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cmake_test PRIVATE scaffold error Catch2::Catch2WithMain)
add_test(NAME cmake_test COMMAND cmake_test)

add_executable(compiler_test compiler_test.cpp)
target_compile_features(compiler_test PRIVATE cxx_std_23)
target_include_directories(compiler_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(compiler_test PRIVATE scaffold error Catch2::Catch2WithMain)
add_test(NAME compiler_test COMMAND compiler_test)

add_executable(releases_test releases_test.cpp)
target_compile_features(releases_test PRIVATE cxx_std_23)
target_include_directories(releases_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(releases_test PRIVATE scaffold error Threads::Threads Catch2::Catch2WithMain)
add_test(NAME releases_test COMMAND releases_test)

add_executable(dockerhub_test dockerhub_test.cpp)
target_compile_features(dockerhub_test PRIVATE cxx_std_23)
target_include_directories(dockerhub_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(dockerhub_test PRIVATE scaffold error Threads::Threads Catch2::Catch2WithMain)
add_test(NAME dockerhub_test COMMAND dockerhub_test)
