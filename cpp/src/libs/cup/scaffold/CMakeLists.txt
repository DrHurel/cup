add_library(scaffold)
target_sources(scaffold
    PRIVATE
        # A module implementation unit, so a plain source rather than a member of
        # the CXX_MODULES file set: it produces no BMI of its own, which is exactly
        # why cup.tmpl and cup.ui live there. See render.cppm.
        Render.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            scaffold.cppm
            naming.cppm
            std.cppm
            render.cppm
)
target_compile_features(scaffold PUBLIC cxx_std_23)
# :render is the one partition that imports cup.tmpl (template lookup) and
# cup.ui (overwrite prompt, status lines); cup.error supplies the E of every
# std::expected result.
target_link_libraries(scaffold PUBLIC error tmpl ui)
