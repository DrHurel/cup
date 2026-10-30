module;
#include <string>
#include <tuple>
export module cup.error;

export import :error;
export import :monad;

// cup.error's Library: the bottom of the graph, with no state.
export namespace cup::error {

struct Library {
    using dependencies = std::tuple<>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
