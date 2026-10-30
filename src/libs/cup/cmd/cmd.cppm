module;
#include <string>
#include <tuple>
export module cup.cmd;

import cup.library;
import core;
import cup.log;
import cup.project;
import cup.scaffold;
import cup.tmpl;
import cup.ui;

export import cup.error;

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

// cup.cmd's Library: everything cup's commands are built from. run_main
// initializes it — and so the whole graph — for the duration of a command.
export namespace cup::cmd {

struct Library {
    using dependencies = std::tuple<
        cup::error::Library,
        cup::library::Library,
        core::Library,
        cup::log::Library,
        cup::project::Library,
        cup::scaffold::Library,
        cup::tmpl::Library,
        cup::ui::Library>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}
