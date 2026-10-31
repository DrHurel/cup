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
        Init.cpp
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
            init.cppm
            add.cppm
            template.cppm  # partition token is :template_cmd, see template.cppm
            completion.cppm
            compiler.cppm
            thirdparty.cppm
            dispatch.cppm
)
target_compile_features(cmd PUBLIC cxx_std_23)
# Build.cpp/Docker.cpp/New.cpp/Add.cpp call project (locate/inspect the
# project), scaffold (rendering, CMakeLists editing, naming), tmpl
# (template-kind lookup), ui (status lines, prompts), core (run_command,
# git init) and logging. They are PUBLIC because cmd.cppm imports each of them
# to name its Library in cmd::Library's dependencies, and an importer of
# cmd needs their BMIs to read cmd's; library is what run_main
# initializes them with. cup_version is a header only Dispatch.cpp includes.
target_link_libraries(cmd PUBLIC error library project scaffold tmpl ui platform logging
                          PRIVATE cup_version)
