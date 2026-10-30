module;
#include <string>
#include <tuple>
export module cup.project;

export import cup.error;

export import :config;
export import :io;

// cup.project's Library.
export namespace cup::project {

struct Library {
    using dependencies = std::tuple<cup::error::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
