module;
#include <string>
#include <tuple>
export module tmpl;

export import error;

export import :corpus;
export import :resolve;

// tmpl's Library.
export namespace tmpl {

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
