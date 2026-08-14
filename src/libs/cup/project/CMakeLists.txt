add_library(project)
target_sources(project
    PRIVATE
        # A module implementation unit, so a plain source rather than a member of
        # the CXX_MODULES file set: it produces no BMI of its own, which is exactly
        # why toml++ lives there. See io.cppm.
        Toml.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            project.cppm
            Config.cppm
            io.cppm
)
target_compile_features(project PUBLIC cxx_std_23)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. toml++ is PRIVATE: it is included in :io's global
# module fragment and appears in no exported signature, so consumers of
# cup.project never see it. cup.error supplies the E of the std::expected results.
target_link_libraries(project PUBLIC error PRIVATE tomlplusplus::tomlplusplus)
