# The POSIX backend — built only on non-Windows hosts (see ../CMakeLists.txt).
add_library(posix)
target_sources(posix
    PRIVATE
        # Module implementation units: fork/exec, termios and libcurl stay in
        # these, out of posix's BMI.
        Process.cpp
        Terminal.cpp
        Http.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            posix.cppm
)
target_compile_features(posix PUBLIC cxx_std_23)
target_link_libraries(posix PUBLIC core PRIVATE CURL::libcurl)

# The composition root: injects this backend into core. An OBJECT library so
# ../CMakeLists.txt can link it as an object file (see the note there).
add_library(posix_services OBJECT PosixServices.cpp)
target_compile_features(posix_services PRIVATE cxx_std_23)
target_link_libraries(posix_services PRIVATE core posix)
