add_library(ui)
target_sources(ui
    PRIVATE
        # A module implementation unit: it #includes FTXUI's headers, which
        # would otherwise enter the BMI of whichever .cppm imported them
        # (same reasoning as project/Toml.cpp for toml++). Nothing about
        # FTXUI appears in cup.ui's exported interface.
        Interactive.cpp
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
# nothing else in cup learns about termios; cup.error supplies the E of its
# std::expected results. ftxui is PRIVATE for the same reason tomlplusplus is in
# project/CMakeLists.txt: it backs Interactive.cpp only.
target_link_libraries(ui PUBLIC platform error PRIVATE ftxui::ftxui)
