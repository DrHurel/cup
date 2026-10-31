module;
#include <string>
#include <tuple>
export module core;

import error;

export import :raw_mode;
export import :services;
export import :terminal;
export import :net;
export import :process;
export import :env;

// core's Library: initialize() prepares the console for cup's output (UTF-8,
// ANSI colours — see Services::init_console).
export namespace core {

struct Library {
    using dependencies = std::tuple<error::Library>;
    static void initialize() { init_console(); }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
