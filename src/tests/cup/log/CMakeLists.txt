add_executable(log_test log_test.cpp)
target_compile_features(log_test PRIVATE cxx_std_23)
target_link_libraries(log_test PRIVATE log test_support Catch2::Catch2WithMain)
add_test(NAME log_test COMMAND log_test)
