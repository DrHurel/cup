add_library(scaffold)
target_sources(scaffold
    PRIVATE
        # Module implementation units, so plain sources rather than members of the
        # CXX_MODULES file set. Every partition's definitions live in one, which is
        # what keeps <format>, <fstream>, <regex>, <chrono> and nlohmann/json out of
        # every BMI — see the note at the top of scaffold.cppm.
        Std.cpp
        Naming.cpp
        Render.cpp
        Cmake.cpp
        Compiler.cpp
        Releases.cpp
        DockerHub.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            scaffold.cppm
            Std.cppm
            Naming.cppm
            Render.cppm
            Cmake.cppm
            Compiler.cppm
            Releases.cppm
            DockerHub.cppm
)
target_compile_features(scaffold PUBLIC cxx_std_23)
# The two release lists are fetched concurrently (std::async in Releases.cpp), so
# the library needs the threading support library. Named rather than assumed: glibc
# 2.34 and musl both fold the pthread symbols into libc, but an older toolchain
# links them separately.
find_package(Threads REQUIRED)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. PUBLIC for cup.error, whose Error is the E of every
# result in the exported interface; PRIVATE for everything only the implementation
# units reach — cup.tmpl and cup.ui behind render(), cup.platform behind the release
# and tag fetches, and nlohmann/json behind the two replies that are JSON.
target_link_libraries(scaffold
    PUBLIC error
    PRIVATE tmpl ui platform nlohmann_json::nlohmann_json Threads::Threads)
