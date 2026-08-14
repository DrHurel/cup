add_executable(cup main.cpp)
target_compile_features(cup PRIVATE cxx_std_23)
target_link_libraries(cup PRIVATE cmd ui error)
