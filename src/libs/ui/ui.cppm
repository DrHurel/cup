module;
#include <string>
#include <string_view>
#include <tuple>
export module ui;

import core;

export import error;

export import :io;
export import :color;
export import :prompt;
export import :select;

// ui's Library.
export namespace ui {

struct Library {
    using dependencies = std::tuple<error::Library, core::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
