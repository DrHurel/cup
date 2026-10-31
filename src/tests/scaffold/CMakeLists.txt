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
target_link_libraries(render_test PRIVATE scaffold tmpl error test_support Catch2::Catch2WithMain)
add_test(NAME render_test COMMAND render_test)

add_executable(cmake_test cmake_test.cpp)
target_compile_features(cmake_test PRIVATE cxx_std_23)
target_link_libraries(cmake_test PRIVATE scaffold error test_support Catch2::Catch2WithMain)
add_test(NAME cmake_test COMMAND cmake_test)

add_executable(compiler_test compiler_test.cpp)
target_compile_features(compiler_test PRIVATE cxx_std_23)
target_link_libraries(compiler_test PRIVATE scaffold error test_support Catch2::Catch2WithMain)
add_test(NAME compiler_test COMMAND compiler_test)

add_executable(releases_test releases_test.cpp)
target_compile_features(releases_test PRIVATE cxx_std_23)
target_link_libraries(releases_test PRIVATE scaffold error test_support Catch2::Catch2WithMain)
add_test(NAME releases_test COMMAND releases_test)

add_executable(dockerhub_test dockerhub_test.cpp)
target_compile_features(dockerhub_test PRIVATE cxx_std_23)
target_link_libraries(dockerhub_test PRIVATE scaffold error test_support Catch2::Catch2WithMain)
add_test(NAME dockerhub_test COMMAND dockerhub_test)
