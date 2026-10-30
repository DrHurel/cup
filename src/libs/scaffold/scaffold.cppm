module;
#include <string>
#include <tuple>
export module scaffold;

import tmpl;
import ui;
import core;

export import error;

export import :naming;
export import :std;
export import :render;
export import :cmake;
export import :compiler;
export import :releases;
export import :dockerhub;

// scaffold's Library.
export namespace scaffold {

struct Library {
    using dependencies = std::tuple<
        error::Library,
        tmpl::Library,
        ui::Library,
        core::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
