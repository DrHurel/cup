# cup.platform's Windows backend — built only on Windows hosts (see
# ../CMakeLists.txt), and injected into the portable core by
# ../services/WindowsServices.cpp.
add_library(platform_windows)
target_sources(platform_windows
    PRIVATE
        # Module implementation units: <windows.h>/<winhttp.h> stay in these,
        # out of cup.platform.windows's BMI.
        Process.cpp
        Console.cpp
        Http.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            windows.cppm
)
target_compile_features(platform_windows PUBLIC cxx_std_23)
# winhttp is the OS's own HTTP client (Http.cpp): no curl/OpenSSL to link.
target_link_libraries(platform_windows PUBLIC platform_core PRIVATE winhttp)
