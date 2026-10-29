# cup.platform is three pieces, wired by dependency injection:
#
#   platform_core     the portable module cup.platform: the Services table
#                     (services.cppm) and the public API that forwards to it.
#                     No OS headers, no #ifdef.
#   platform_posix    the OS-specific implementations — exactly one of the two
#   platform_windows  is built, for the host platform (posix/, windows/).
#   platform_services the composition root (services/*Services.cpp): defines
#                     make_platform_services(), injecting the chosen backend
#                     into the core. Also exactly one, for the host platform.
#
# Everything else in cup links the `platform` umbrella below and imports
# cup.platform, never a backend, so it compiles unchanged on every OS.
add_library(platform_core)
target_sources(platform_core
    PUBLIC
        FILE_SET CXX_MODULES FILES
            platform.cppm
            raw_mode.cppm
            services.cppm
            Terminal.cppm
            net.cppm
            process.cppm
            env.cppm
)
target_compile_features(platform_core PUBLIC cxx_std_23)
# PUBLIC because cup.error types appear in cup.platform's exported interface
# (std::expected<RawMode, Error>, std::expected<std::string, Error>).
target_link_libraries(platform_core PUBLIC error)

if(WIN32)
  add_subdirectory(windows)
  set(_cup_platform_backend platform_windows)
  set(_cup_platform_services services/WindowsServices.cpp)
else()
  add_subdirectory(posix)
  set(_cup_platform_backend platform_posix)
  set(_cup_platform_services services/PosixServices.cpp)
endif()

# An OBJECT library, linked into every consumer as an object file rather than
# an archive member: platform_core calls make_platform_services() but sits
# after it on the link line, and a static archive's member is only pulled in
# for a symbol still undefined when the archive is scanned. An object file is
# always linked whole, wherever it sits — and CMake lists it once, however many
# of cup's libraries link `platform`.
add_library(platform_services OBJECT ${_cup_platform_services})
target_compile_features(platform_services PRIVATE cxx_std_23)
target_link_libraries(platform_services PRIVATE platform_core ${_cup_platform_backend})

add_library(platform INTERFACE)
target_link_libraries(platform
    INTERFACE $<TARGET_OBJECTS:platform_services> ${_cup_platform_backend} platform_core)
add_dependencies(platform platform_services)
