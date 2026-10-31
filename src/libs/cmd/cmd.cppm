module;
#include <string>
#include <tuple>
export module cmd;

import library;
import core;
import logging;
import project;
import scaffold;
import tmpl;
import ui;

export import error;

export import :build;
export import :docker;
export import :new_project;
export import :init;
export import :add;
export import :template_cmd;
export import :completion;
export import :compiler;
export import :thirdparty;
export import :dispatch;

// cmd's Library: everything cup's commands are built from. run_main
// initializes it — and so the whole graph — for the duration of a command.
export namespace cmd {

struct Library {
    using dependencies = std::tuple<
        error::Library,
        library::Library,
        core::Library,
        logging::Library,
        project::Library,
        scaffold::Library,
        tmpl::Library,
        ui::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
