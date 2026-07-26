add_executable(ui_test ui_test.cpp)
target_compile_features(ui_test PRIVATE cxx_std_23)
# cup scaffolds the target but not its dependencies: the module under test and
# Catch2's bundled main().
target_link_libraries(ui_test PRIVATE ui error probe Catch2::Catch2WithMain)
add_test(NAME ui_test COMMAND ui_test)
