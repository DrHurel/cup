add_executable(cup main.cpp)
target_compile_features(cup PRIVATE cxx_std_23)
target_link_libraries(cup PRIVATE cmd ui error)

# Only meaningful when driven by `cpack` (the .deb job) or a manual `cmake
# --install`; a plain `cup build`/ninja build never touches this.
install(TARGETS cup RUNTIME DESTINATION bin)
