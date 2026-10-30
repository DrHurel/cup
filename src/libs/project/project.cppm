module;
#include <string>
#include <tuple>
export module project;

export import error;

export import :config;
export import :io;

// project's Library.
export namespace project {

struct Library {
    using dependencies = std::tuple<error::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
