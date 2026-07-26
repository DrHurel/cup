// cup.tmpl holds the built-in scaffolding templates, embedded into the cup
// binary, and resolves them against per-project overrides.
//
// A template "kind" (class, interface, app, test, …) is a directory of
// {{placeholder}} files. cup ships a default set in the embedded corpus; a project
// may add its own kinds — or override a built-in — by dropping a directory of the
// same shape into .cup/templates/<kind>/ at its root. Resolution always prefers
// the project copy.
//
// The implementation splits along the one seam that matters: :corpus reads the
// binary's embedded copy and touches no disk at all, while :resolve layers a
// project's on-disk overrides over it. That is also what keeps the stream headers
// (<fstream>) confined to a single partition — see the note in ui.cppm for why
// GCC 14 requires that discipline.
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too.
module;
#include <string>
export module cup.tmpl;

// Re-exported because cup::error::Error is the E of read()'s and copy_builtin()'s
// std::expected results.
export import cup.error;

export import :corpus;
export import :resolve;
