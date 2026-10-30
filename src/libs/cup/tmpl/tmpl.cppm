module;
#include <string>
#include <tuple>
export module cup.tmpl;

export import cup.error;

export import :corpus;
export import :resolve;

// cup.tmpl's Library.
export namespace cup::tmpl {

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
