add_library(probe)
target_sources(probe
    PUBLIC
        FILE_SET CXX_MODULES FILES
            probe.cppm
            Probe.cppm
)
target_compile_features(probe PUBLIC cxx_std_20)
