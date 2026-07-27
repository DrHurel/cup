# utils.error is a library in its own right rather than a fifth partition of this
# one: every module in cup imports it and none of them wants a thread pool, so
# folding it in here would put <future> and <memory> in the BMI of a project that
# asked for an error type. Nested under utils/ because the path is what names it —
# src/libs/utils/error -> module utils.error, namespace utils::error.
add_subdirectory(error)

add_library(utils)
target_sources(utils
    PRIVATE
        # Module implementation units, so plain sources rather than members of the
        # CXX_MODULES file set: they produce no BMI of their own. That is what keeps
        # <thread>, <mutex> and <condition_variable> out of every consumer (Pool.cpp)
        # and what makes the locator's std::unordered_map instantiate in an ordinary
        # translation unit, which on GCC 14 is the difference between compiling and
        # not (Service.cpp — see the note on store() in Service.cppm).
        Service.cpp
        Pool.cpp
    PUBLIC
        FILE_SET CXX_MODULES FILES
            utils.cppm
            Singleton.cppm
            Service.cppm
            Factory.cppm
            Pool.cppm
            StrongType.cppm
)
target_compile_features(utils PUBLIC cxx_std_23)
# ThreadPool runs std::jthread workers. Named rather than assumed: glibc 2.34 and
# musl both fold the pthread symbols into libc, so this resolves to nothing on the
# toolchains cup builds against, but an older one links them separately — and here
# it is PUBLIC, unlike in cup.scaffold, because utils is a static library whose
# threading symbols are resolved at the *consumer's* link.
find_package(Threads REQUIRED)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. PUBLIC for both: utils::error::Error is the E of the
# results ServiceLocator::get and Factory::create return, so it is part of this
# library's exported interface.
target_link_libraries(utils PUBLIC error Threads::Threads)
