add_library(ui)
target_sources(ui
    PUBLIC
        FILE_SET CXX_MODULES FILES
            ui.cppm
)
target_compile_features(ui PUBLIC cxx_std_20)
# cup.ui isolates the terminal behind cup.platform, so nothing else in cup learns
# about termios; cup.error supplies the E of its std::expected results.
target_link_libraries(ui PUBLIC platform error)
