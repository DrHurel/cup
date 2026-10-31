add_executable(cmd_test cmd_test.cpp)
target_compile_features(cmd_test PRIVATE cxx_std_23)
target_link_libraries(cmd_test PRIVATE cmd project scaffold ui platform error test_support
                                       Catch2::Catch2WithMain)
add_test(NAME cmd_test COMMAND cmd_test)

add_executable(new_add_test new_add_test.cpp)
target_compile_features(new_add_test PRIVATE cxx_std_23)
target_link_libraries(new_add_test PRIVATE cmd project scaffold tmpl ui platform error test_support
                                           Catch2::Catch2WithMain)
add_test(NAME new_add_test COMMAND new_add_test)

add_executable(cmd_docker_test cmd_docker_test.cpp)
target_compile_features(cmd_docker_test PRIVATE cxx_std_23)
target_link_libraries(cmd_docker_test PRIVATE cmd project scaffold ui error test_support
                                              Catch2::Catch2WithMain)
add_test(NAME cmd_docker_test COMMAND cmd_docker_test)

add_executable(template_completion_test template_completion_test.cpp)
target_compile_features(template_completion_test PRIVATE cxx_std_23)
target_link_libraries(template_completion_test PRIVATE cmd project tmpl ui platform error
                                                       test_support Catch2::Catch2WithMain)
add_test(NAME template_completion_test COMMAND template_completion_test)

add_executable(compiler_cmd_test compiler_cmd_test.cpp)
target_compile_features(compiler_cmd_test PRIVATE cxx_std_23)
target_link_libraries(compiler_cmd_test PRIVATE cmd project scaffold platform error test_support
                                                Catch2::Catch2WithMain)
add_test(NAME compiler_cmd_test COMMAND compiler_cmd_test)

add_executable(docker_cmd_test docker_cmd_test.cpp)
target_compile_features(docker_cmd_test PRIVATE cxx_std_23)
target_link_libraries(docker_cmd_test PRIVATE cmd project scaffold platform ui error test_support
                                              Catch2::Catch2WithMain)
add_test(NAME docker_cmd_test COMMAND docker_cmd_test)

add_executable(thirdparty_cmd_test thirdparty_cmd_test.cpp)
target_compile_features(thirdparty_cmd_test PRIVATE cxx_std_23)
target_link_libraries(thirdparty_cmd_test PRIVATE cmd project platform ui error test_support
                                                  Catch2::Catch2WithMain)
add_test(NAME thirdparty_cmd_test COMMAND thirdparty_cmd_test)

add_executable(init_test init_test.cpp)
target_compile_features(init_test PRIVATE cxx_std_23)
target_link_libraries(init_test PRIVATE cmd project scaffold ui platform error test_support
                                        Catch2::Catch2WithMain)
add_test(NAME init_test COMMAND init_test)
