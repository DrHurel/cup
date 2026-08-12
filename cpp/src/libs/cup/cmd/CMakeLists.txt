add_library(cmd)
target_sources(cmd
    PRIVATE
        # Module implementation units, so plain sources rather than members of the
        # CXX_MODULES file set: they produce no BMI of their own. See build.cppm /
        # dispatch.cppm.
        Build.cpp
        Dispatch.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            cmd.cppm
            build.cppm
            dispatch.cppm
)
target_compile_features(cmd PUBLIC cxx_std_23)
# Build.cpp calls cup.project (locate/inspect the project), cup.scaffold
# (list_subdirs, for `run`'s app picker) and cup.ui (status lines, the
# interactive app picker) and cup.platform (run_command). None of those types
# appear in cup.cmd's own exported interface (Command's Run is a
# std::function<..., cup::error::Error>), so all four are PRIVATE; only
# cup.error is PUBLIC.
target_link_libraries(cmd PUBLIC error PRIVATE project scaffold ui platform)
