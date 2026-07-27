// Primary interface unit for utils.error.
//
// It sits at src/libs/utils/error rather than src/libs/cup/error, which is what
// makes it `utils.error` in namespace utils::error: module names come from the path
// under src/libs, and an E that carries a message and a sentinel Kind is not a cup
// concept — nothing in it knows about projects, templates or the terminal. Every
// library under cup/ imports it; it imports nothing, from cup or anywhere else,
// which is why the dependency can only run one way.
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too. `cup add lib`
// now scaffolds this preamble automatically (see cmd.primaryPreamble).
module;
#include <string>
export module utils.error;

export import :error;
export import :monad;
