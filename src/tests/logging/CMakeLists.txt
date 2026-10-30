add_executable(logging_test logging_test.cpp)
target_compile_features(logging_test PRIVATE cxx_std_23)
target_link_libraries(logging_test PRIVATE logging test_support Catch2::Catch2WithMain)
add_test(NAME logging_test COMMAND logging_test)
