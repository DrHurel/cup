module;
#include <string>
#include <tuple>
export module error;

export import :error;
export import :monad;

// error's Library: the bottom of the graph, with no state.
export namespace error {

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
