# cup.platform's POSIX backend — built only on non-Windows hosts (see
# ../CMakeLists.txt), and injected into the portable core by
# ../services/PosixServices.cpp.
add_library(platform_posix)
target_sources(platform_posix
    PRIVATE
        # Module implementation units: fork/exec, termios and libcurl stay in
        # these, out of cup.platform.posix's BMI.
        Process.cpp
        Terminal.cpp
        Http.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            posix.cppm
)
target_compile_features(platform_posix PUBLIC cxx_std_23)
target_link_libraries(platform_posix PUBLIC platform_core PRIVATE CURL::libcurl)
