# The platform layer: three libraries wired by dependency injection.
#
#   core     the portable module core: the Services table (services.cppm) and
#            the public API that forwards to it. No OS headers, no #ifdef.
#   posix    the OS-specific backends — exactly one of the two is built, for
#   windows  the host platform. Each also owns its composition root
#            (PosixServices.cpp / WindowsServices.cpp), the services file that
#            defines make_platform_services() and so injects that backend into
#            core.
#
# Everything in cup links the `platform` umbrella below and imports core, never
# a backend, so it compiles unchanged on every OS.
add_subdirectory(core)
if(WIN32)
  add_subdirectory(windows)
  set(_cup_platform_backend windows)
else()
  add_subdirectory(posix)
  set(_cup_platform_backend posix)
endif()

# The services file is linked into every consumer as an object file rather than
# an archive member: core calls make_platform_services() but sits after it on
# the link line, and a static archive's member is only pulled in for a symbol
# still undefined when the archive is scanned. An object file is always linked
# whole, wherever it sits — and CMake lists it once, however many of cup's
# libraries link `platform`.
add_library(platform INTERFACE)
target_link_libraries(platform
    INTERFACE $<TARGET_OBJECTS:${_cup_platform_backend}_services> ${_cup_platform_backend} core)
add_dependencies(platform ${_cup_platform_backend}_services)

add_subdirectory(cup)
