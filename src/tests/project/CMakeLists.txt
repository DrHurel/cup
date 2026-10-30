add_executable(project_test project_test.cpp)
target_compile_features(project_test PRIVATE cxx_std_23)
# The dogfood parity case re-encodes cup's own cup.toml, which the Go cup wrote.
target_compile_definitions(project_test PRIVATE CUP_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")
target_link_libraries(project_test PRIVATE project error test_support Catch2::Catch2WithMain)
add_test(NAME project_test COMMAND project_test)
