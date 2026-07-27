add_library(error)
target_sources(error
    PUBLIC
        FILE_SET CXX_MODULES FILES
            error.cppm
            Error.cppm
            monad.cppm
)
target_compile_features(error PUBLIC cxx_std_23)
