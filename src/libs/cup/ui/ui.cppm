module;
#include <string>
#include <string_view>
#include <tuple>
export module cup.ui;

import core;

export import cup.error;

export import :io;
export import :color;
export import :prompt;
export import :select;

// cup.ui's Library.
export namespace cup::ui {

struct Library {
    using dependencies = std::tuple<cup::error::Library, core::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
