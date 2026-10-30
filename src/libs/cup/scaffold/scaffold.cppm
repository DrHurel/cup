module;
#include <string>
#include <tuple>
export module cup.scaffold;

import cup.tmpl;
import cup.ui;
import core;

export import cup.error;

export import :naming;
export import :std;
export import :render;
export import :cmake;
export import :compiler;
export import :releases;
export import :dockerhub;

// cup.scaffold's Library.
export namespace cup::scaffold {

struct Library {
    using dependencies = std::tuple<
        cup::error::Library,
        cup::tmpl::Library,
        cup::ui::Library,
        core::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
