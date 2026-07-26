// cup.ui is cup's interactive layer: an arrow-key menu, a y/n confirm, a validated
// text input, and the coloured "wrote / updated / skipped" log. The implementation
// lives in four partitions — :io, :color, :prompt, :select.
//
// Two GCC 14 constraints shape that split, and both are worth knowing before
// adding a partition here or scaffolding another partitioned module.
//
// 1. This file's global module fragment is load-bearing, even though the file
//    declares nothing itself. A module whose partitions carry a global module
//    fragment must carry one too, or GCC 14 writes a primary BMI its *consumers*
//    cannot read:
//
//        error: failed to read compiled module cluster N: Bad file data
//        fatal error: failed to load pendings for 'std::basic_string_view'
//
//    The failure always lands on the consumer, never here, and it fires even when
//    the consumer includes no standard headers of its own. A token fragment does
//    not count — `#include <cstddef>` still fails where `<string>` succeeds.
//
// 2. At most one partition may use <print>, <format> or <iostream>. When two do,
//    GCC 14 either fails as above or gives up outright ("returning to the gate for
//    a mechanical issue"). Light headers — <string>, <string_view>, <vector>,
//    <expected> — repeat across partitions without trouble. That is why :io owns
//    every read and write, and the other partitions build plain strings and hand
//    them to it.
//
// Note also that `cup add lib` generates a primary aggregator with no global
// module fragment at all — exactly the shape point 1 forbids — so every
// partitioned module cup scaffolds needs this preamble added by hand until the
// compiler floor moves past GCC 14.
module;
#include <string>
#include <string_view>
export module cup.ui;

// Re-exported because cup::error::Error appears in every prompt's return type.
export import cup.error;

export import :io;
export import :color;
export import :prompt;
export import :select;
