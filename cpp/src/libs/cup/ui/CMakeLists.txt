add_library(ui)
target_sources(ui
    PUBLIC
        FILE_SET CXX_MODULES FILES
            ui.cppm
            io.cppm
            Color.cppm
            prompt.cppm
            select.cppm
)
target_compile_features(ui PUBLIC cxx_std_23)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. cup.ui isolates the terminal behind cup.platform, so
# nothing else in cup learns about termios; utils.error supplies the E of its
# std::expected results.
target_link_libraries(ui PUBLIC platform error)
