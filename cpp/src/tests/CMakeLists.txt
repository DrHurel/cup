add_executable(ui_test ui_test.cpp)
target_compile_features(ui_test PRIVATE cxx_std_23)
# cup scaffolds the target but not its dependencies: the module under test and
# Catch2's bundled main().
target_link_libraries(ui_test PRIVATE ui error probe Catch2::Catch2WithMain)
add_test(NAME ui_test COMMAND ui_test)

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
