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
// project's on-disk overrides over it. That is also what keeps the stream and
// format headers (<fstream>, <format>) confined to a single partition — see the
// note in ui.cppm for why GCC 14 requires that discipline. :corpus therefore
// builds its lookup keys and prefixes with detail::concat rather than with
// std::format; that is the constraint talking, not a style preference.
//
// The constraint is tighter than "no two partitions may include <format>", which
// is worth spelling out because the obvious workaround looks like it should work
// and does not. Two arrangements were built against GCC 14.2, and both failed:
//
//   - <format> in :corpus while :resolve keeps <format> too, and
//   - <format> in :corpus while :resolve drops <format> entirely and formats
//     through a format_text shim exported from :corpus — the cup.ui:io trick,
//     which :resolve is free to use because it already imports :corpus.
//
// The second is the interesting one: after it, <format> appears in exactly one
// fragment, and it still fails. :resolve's remaining <fstream> is enough on its
// own to be the second partition. Both end at the same wall, when the primary
// below reads its own partition's BMI:
//
//     cup.tmpl:resolve: error: failed to read compiled module cluster 2996:
//                              Bad file data
//     fatal error: failed to load pendings for 'std::_Mutex_base'
//
// So the rule is one partition per module for the whole format/stream family,
// not one per header — and :resolve has to be that partition, because it owns
// the file I/O and cannot give up <fstream>. That leaves :corpus with no route
// to std::format at all, shim or otherwise, short of moving the formatting into
// a separate module (untried; a new module for three path joins is not a trade
// worth making).
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too.
module;
#include <string>
export module cup.tmpl;

// Re-exported because utils::error::Error is the E of read()'s and copy_builtin()'s
// std::expected results.
export import utils.error;

export import :corpus;
export import :resolve;
