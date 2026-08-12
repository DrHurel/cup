add_library(platform)
target_sources(platform
    PRIVATE
        # A module implementation unit, so a plain source rather than a member of
        # the CXX_MODULES file set: it produces no BMI of its own, which is exactly
        # why libcurl lives there. See net.cppm.
        Http.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            platform.cppm
            Terminal.cppm
            net.cppm
)
target_compile_features(platform PUBLIC cxx_std_23)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. PUBLIC because cup.error types appear in
# cup.platform's exported interface (std::expected<RawMode, Error>,
# std::expected<std::string, Error>). CURL::libcurl is PRIVATE: it is included
# in :net's implementation unit (Http.cpp) and appears in no exported signature.
target_link_libraries(platform PUBLIC error PRIVATE CURL::libcurl)
