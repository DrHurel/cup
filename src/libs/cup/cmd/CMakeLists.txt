add_library(cmd)
target_sources(cmd
    PRIVATE
        # Module implementation units, so plain sources rather than members of the
        # CXX_MODULES file set: they produce no BMI of their own. See build.cppm /
        # dispatch.cppm.
        Build.cpp
        Dispatch.cpp
        Docker.cpp
        New.cpp
        Add.cpp
        Template.cpp
        Completion.cpp
        Compiler.cpp
        Thirdparty.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            cmd.cppm
            build.cppm
            docker.cppm
            new.cppm  # partition token is :new_project, see new.cppm
            add.cppm
            template.cppm  # partition token is :template_cmd, see template.cppm
            completion.cppm
            compiler.cppm
            thirdparty.cppm
            dispatch.cppm
)
target_compile_features(cmd PUBLIC cxx_std_23)
# Build.cpp/Docker.cpp/New.cpp/Add.cpp call cup.project (locate/inspect the
# project), cup.scaffold (rendering, CMakeLists editing, naming), cup.tmpl
# (template-kind lookup), cup.ui (status lines, prompts) and cup.platform
# (run_command, git init). None of those types appear in cup.cmd's own
# exported interface (Command's Run is a std::function<..., cup::error::Error>),
# so all are PRIVATE; only cup.error is PUBLIC.
target_link_libraries(cmd PUBLIC error PRIVATE project scaffold tmpl ui platform log cup_version)
