# The ui suite drives a real pseudo-terminal (posix_openpt), which Windows
# doesn't have.
if(NOT WIN32)
  add_executable(ui_test ui_test.cpp)
  target_compile_features(ui_test PRIVATE cxx_std_23)
  # cup scaffolds the target but not its dependencies: the module under test and
  # Catch2's bundled main(). platform is named because the suite imports core
  # itself to check the terminal condition select_one branches on — ui imports it
  # without re-exporting it. ftxui backs Interactive.hpp, #included directly (not via
  # the ui module, hence the include directory) so the suite can drive its
  # component trees with synthetic events — see Interactive.hpp's header comment.
  target_include_directories(ui_test PRIVATE "${PROJECT_SOURCE_DIR}/src/libs/ui")
  target_link_libraries(ui_test PRIVATE ui error platform test_support
                                        ftxui::ftxui Catch2::Catch2WithMain)
  add_test(NAME ui_test COMMAND ui_test)
endif()
