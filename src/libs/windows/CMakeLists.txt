# The Windows backend — built only on Windows hosts (see ../CMakeLists.txt).
add_library(windows)
target_sources(windows
    PRIVATE
        # Module implementation units: <windows.h>/<winhttp.h> stay in these,
        # out of windows's BMI.
        Process.cpp
        Console.cpp
        Http.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            windows.cppm
)
target_compile_features(windows PUBLIC cxx_std_23)
# winhttp is the OS's own HTTP client (Http.cpp): no curl/OpenSSL to link.
target_link_libraries(windows PUBLIC core PRIVATE winhttp)

# The composition root: injects this backend into core. An OBJECT library so
# ../CMakeLists.txt can link it as an object file (see the note there).
add_library(windows_services OBJECT WindowsServices.cpp)
target_compile_features(windows_services PRIVATE cxx_std_23)
target_link_libraries(windows_services PRIVATE core windows)
