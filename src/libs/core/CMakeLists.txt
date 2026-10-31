# core: the portable half of the platform layer (see ../CMakeLists.txt). It
# calls make_platform_services() but never defines it — the host's backend
# (../posix or ../windows) does, in its services file.
add_library(core)
target_sources(core
    PUBLIC
        FILE_SET CXX_MODULES FILES
            core.cppm
            raw_mode.cppm
            services.cppm
            Terminal.cppm
            net.cppm
            process.cppm
            env.cppm
)
target_compile_features(core PUBLIC cxx_std_23)
# PUBLIC because error types appear in core's exported interface
# (std::expected<RawMode, Error>, std::expected<std::string, Error>).
target_link_libraries(core PUBLIC error)
