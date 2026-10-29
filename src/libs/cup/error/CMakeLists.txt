add_library(error)
target_sources(error
    PUBLIC
        FILE_SET CXX_MODULES FILES
            error.cppm
            # Not Error.cppm: that and error.cppm are one file on a
            # case-insensitive filesystem (Windows, macOS) — see Add.cpp's
            # component_stem, which scaffolds the same way.
            error-Error.cppm
            monad.cppm
)
target_compile_features(error PUBLIC cxx_std_23)
