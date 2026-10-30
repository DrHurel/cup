# Links cmd for the graph assertions on cup's real libraries: cup::cmd::Library
# reaches every other one.
add_executable(library_test library_test.cpp)
target_compile_features(library_test PRIVATE cxx_std_23)
target_link_libraries(library_test PRIVATE library cmd Catch2::Catch2WithMain)
add_test(NAME library_test COMMAND library_test)
