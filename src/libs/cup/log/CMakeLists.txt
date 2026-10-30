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
            log-Log.cppm  # not Log.cppm: see error/CMakeLists.txt
)
target_compile_features(log PUBLIC cxx_std_23)
# cup.error supplies the E of init()'s std::expected result. platform resolves
# the log directory (core::cache_dir); it is PUBLIC because log.cppm imports
# core to name core::Library in cup::log::Library's dependencies.
target_link_libraries(log PUBLIC error platform PRIVATE spdlog::spdlog)
