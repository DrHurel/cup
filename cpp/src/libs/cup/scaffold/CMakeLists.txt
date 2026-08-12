add_library(scaffold)
target_sources(scaffold
    PRIVATE
        # Module implementation units, so plain sources rather than members of the
        # CXX_MODULES file set: they produce no BMI of their own, which is exactly
        # why cup.tmpl/cup.ui (Render.cpp) and <regex>/<filesystem> (Cmake.cpp) live
        # there. See render.cppm / cmake.cppm.
        Render.cpp
        Cmake.cpp
        Compiler.cpp
        Releases.cpp
        Dockerhub.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            scaffold.cppm
            naming.cppm
            std.cppm
            render.cppm
            cmake.cppm
            compiler.cppm
            releases.cppm
            dockerhub.cppm
)
target_compile_features(scaffold PUBLIC cxx_std_23)
# glibc/musl fold pthread's symbols into libc on the toolchains cup builds
# against (see src/tests/CMakeLists.txt's note on ui_test), but Threads::Threads
# is still how CMake spells "link against the platform's thread support".
find_package(Threads REQUIRED)
# :render is the one partition that imports cup.tmpl (template lookup) and
# cup.ui (overwrite prompt, status lines); :releases and :dockerhub import
# cup.platform (http_get); :releases' Releases.cpp is the only file using
# std::thread directly. cup.error supplies the E of every std::expected
# result.
target_link_libraries(scaffold PUBLIC error tmpl ui platform PRIVATE Threads::Threads)
