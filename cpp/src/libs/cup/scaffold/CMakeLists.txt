add_library(scaffold)
target_sources(scaffold
    PUBLIC
        FILE_SET CXX_MODULES FILES
            scaffold.cppm
            naming.cppm
            std.cppm
)
target_compile_features(scaffold PUBLIC cxx_std_23)
target_link_libraries(scaffold PUBLIC error)
