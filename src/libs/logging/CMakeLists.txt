add_library(logging)
target_sources(logging
    PRIVATE
        # A module implementation unit: it #includes spdlog's headers, which
        # would otherwise enter the BMI of whichever .cppm imported them
        # (same reasoning as project/Toml.cpp for toml++). Nothing about
        # spdlog appears in logging's exported interface.
        Spdlog.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            logging.cppm
            Log.cppm
)
target_compile_features(logging PUBLIC cxx_std_23)
# error supplies the E of init()'s std::expected result. platform resolves
# the log directory (core::cache_dir); it is PUBLIC because logging.cppm imports
# core to name core::Library in logging::Library's dependencies.
target_link_libraries(logging PUBLIC error platform PRIVATE spdlog::spdlog)
