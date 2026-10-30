module;
#include <string>
#include <tuple>
export module logging;

import error;
import core;

export import :log;

// logging's Library: finalize() closes the log file (see shutdown()). Opening
// it is left to init(), which can fail and whose failure the caller reports
// to the user — run_main does, right after initializing the libraries.
export namespace logging {

struct Library {
    using dependencies = std::tuple<error::Library, core::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() { shutdown(); }
};

}
