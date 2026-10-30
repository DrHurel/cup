add_library(library)
target_sources(library
    PUBLIC
        FILE_SET CXX_MODULES FILES
            library.cppm
)
target_compile_features(library PUBLIC cxx_std_23)
