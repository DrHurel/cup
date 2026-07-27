add_library(platform)
target_sources(platform
    PRIVATE
        # A module implementation unit, so a plain source rather than a member of
        # the CXX_MODULES file set: it produces no BMI of its own, which is exactly
        # why libcurl lives there. See Http.cppm.
        Http.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            platform.cppm
            Terminal.cppm
            Http.cppm
)
target_compile_features(platform PUBLIC cxx_std_23)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. PUBLIC because utils.error types appear in
# cup.platform's exported interface (std::expected<RawMode, Error>). libcurl is
# PRIVATE: it is included only in Http.cpp and named in no exported signature, so
# consumers of cup.platform never see it — which is what keeps "ship without
# libcurl" a supported degradation rather than a rewrite.
target_link_libraries(platform PUBLIC error PRIVATE CURL::libcurl)
