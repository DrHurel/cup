add_executable(tmpl_test tmpl_test.cpp)
target_compile_features(tmpl_test PRIVATE cxx_std_23)
target_link_libraries(tmpl_test PRIVATE tmpl error test_support Catch2::Catch2WithMain)
add_test(NAME tmpl_test COMMAND tmpl_test)
