module;
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
export module cmd:init;

export import error;

// Declarations only, defined in Init.cpp: the definitions import
// scaffold and ui and use <regex>/<filesystem> throughout, none of
// which this interface partition needs to expose.
export namespace cmd {

// Detected is what cup can tell about an existing project from its build
// files alone. Each field is empty/0 when nothing conclusive was found.
struct Detected {
    std::string build_tool;  // project::kToolCMake / kToolMake, or "" (no build file)
    std::string name;        // the root CMakeLists' project() name, or ""
    int standard = 0;        // the newest C++ standard the build files ask for, or 0
};

// detect_project inspects root's build files: a root CMakeLists.txt makes it
// a CMake project, else a Makefile (makefile, GNUmakefile) a Make one. The
// name comes from the root CMakeLists' project() call; the standard from
// every CMakeLists.txt in the tree (CMake) or the root Makefile (Make).
[[nodiscard]] Detected detect_project(const std::filesystem::path& root);

// parse_std_token maps a C++ standard as build files spell it — "17",
// "2a", "1z", "0x", ... — onto one of cup's supported standards, or nullopt
// for anything else (98/03, 26, garbage).
[[nodiscard]] std::optional<int> parse_std_token(std::string_view token);

// detect_cmake_standard returns the newest standard a CMakeLists' text asks
// for via CMAKE_CXX_STANDARD / CXX_STANDARD, cxx_std_NN compile features, or
// a -std=c++NN flag; 0 if it names none cup supports.
[[nodiscard]] int detect_cmake_standard(std::string_view text);

// detect_make_standard does the same for a Makefile's -std=c++NN / gnu++NN.
[[nodiscard]] int detect_make_standard(std::string_view text);

// detect_cmake_name returns the name passed to the first project() call in
// a CMakeLists' text (CMake commands are case-insensitive), or "" if there
// is none or it is not a literal (e.g. project(${NAME})).
[[nodiscard]] std::string detect_cmake_name(std::string_view text);

// sanitize_ident turns a directory name into a C++ identifier cup accepts as
// a project name: every character outside [A-Za-z0-9_] becomes '_', and a
// leading digit gets a '_' prefix. "" stays "".
[[nodiscard]] std::string sanitize_ident(std::string_view raw);

// with_compiler_guard inserts an empty cup compiler-guard block (see
// scaffold::compiler_guard) right after the first project() call in a
// CMakeLists' text, so `cup compiler set` can manage floors later. Returns
// nullopt when there is no project() call to anchor it to. The guard must
// follow project(): CMAKE_CXX_COMPILER_ID is only known from there on.
[[nodiscard]] std::optional<std::string> with_compiler_guard(std::string_view text);

// run_init turns the current directory — an existing C++ project — into a
// cup project in place: `cup init [name] [--std N] [--build-tool cmake|make]`.
// It writes cup.toml from what detect_project finds (flags win, prompts fill
// any gaps), then wires the existing build in without restructuring it: a
// CMake project gains a compiler-guard block and, where they don't exist
// yet, the src/libs and src/apps directories `cup add` registers into; a
// Make project's Makefile is left exactly as it is. A directory with no build
// file at all gets the same fresh tree `cup new` scaffolds. Existing sources
// are never moved or rewritten.
[[nodiscard]] std::expected<void, error::Error> run_init(std::span<const std::string> args);

}
