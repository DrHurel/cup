add_library(log)
target_sources(log
    PRIVATE
        # A module implementation unit: it #includes spdlog's headers, which
        # would otherwise enter the BMI of whichever .cppm imported them
        # (same reasoning as project/Toml.cpp for toml++). Nothing about
        # spdlog appears in cup.log's exported interface.
        Spdlog.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            log.cppm
            Log.cppm
)
target_compile_features(log PUBLIC cxx_std_23)
target_link_libraries(log PRIVATE spdlog::spdlog)
