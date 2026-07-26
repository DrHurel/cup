// Primary interface unit for cup.platform.
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too. `cup add lib`
// now scaffolds this preamble automatically (see cmd.primaryPreamble).
module;
#include <string>
export module cup.platform;

export import :terminal;
