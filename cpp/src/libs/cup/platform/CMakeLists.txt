add_library(platform)
target_sources(platform
    PUBLIC
        FILE_SET CXX_MODULES FILES
            platform.cppm
            Terminal.cppm
)
target_compile_features(platform PUBLIC cxx_std_23)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. PUBLIC because cup.error types appear in
# cup.platform's exported interface (std::expected<RawMode, Error>).
target_link_libraries(platform PUBLIC error)
