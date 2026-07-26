// cup.project locates and describes a cup project: the directory tree rooted at
// the nearest ancestor holding a cup.toml marker.
//
// :config is the data model — plain structs plus the predicates that give unset
// fields their meaning — and :io is the disk and TOML seam. Keeping them apart is
// what lets cup.scaffold and cup.cmd depend on the model without importing the
// parser.
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too.
module;
#include <string>
export module cup.project;

// Re-exported because cup::error::Error is the E of every result :io returns.
export import cup.error;

export import :config;
export import :io;
