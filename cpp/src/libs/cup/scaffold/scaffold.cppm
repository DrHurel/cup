// cup.scaffold is the pure core of cup: it turns a project's decisions — its C++
// standard, its template kinds, its compiler floor — into files, and edits the
// build files that already exist. Everything `cup new`, `cup add`, `cup compiler`
// and `cup register` write passes through here.
//
// The partitions follow the Go package's files one for one:
//
//   :std        the C++ standard and the template variables it implies
//   :naming     identifiers, namespaces and module names derived from paths
//   :render     template substitution, and writing the result to disk
//   :cmake      surgical edits to an existing CMakeLists.txt
//   :compiler   the compiler-version guard block
//   :releases   the newest released GCC/Clang, for the floor picker
//   :dockerhub  a repository's tags, for the base-image picker
//
// Every partition interface below is *declarations only*, with the definitions in a
// module implementation unit of the same name (Std.cppm / Std.cpp, and so on). That
// is not a style preference — three GCC 14 constraints force it, and all three cost
// real time to diagnose because none of them reports the line that caused it:
//
// 1. A large third-party header in an interface unit's global module fragment makes
//    the compiler ICE while the primary merges the partition (Phase 2 hit this with
//    toml++; here it would be <curl/curl.h> and toml++ again). An implementation
//    unit's fragment never reaches a BMI, so the dependency is invisible there.
//
// 2. At most one partition of a module may reach the heavy standard library —
//    <format>, <fstream>, <iostream>, <print>, <regex> and what they drag in — or
//    the primary fails to read its own partition's BMI ("failed to read compiled
//    module cluster N: Bad file data"). With every definition in an implementation
//    unit, no partition needs any of them, and the question does not arise.
//
//    <filesystem> is the one that still has to be rationed, because it is in the
//    *signatures*: it repeats across partitions, but not without limit. Four of the
//    seven below carry it (:naming, :render, :cmake, :compiler) and merge fine; the
//    fifth — :releases, for a std::optional<std::filesystem::path> on one detail
//    function — produced exactly the failure above, and returning that path as a
//    std::string instead made it go away with nothing else changed. So the header
//    is not free, and a partition that needs it only for a convenience should not
//    take it. Everything else the interfaces use (<string>, <string_view>,
//    <vector>, <optional>, <expected>, <map>, <functional>, <array>, <cstdint>)
//    repeats without trouble.
//
// 3. An *inline* function defined in an interface unit and returning
//    std::expected<std::string, Error> — or a std::function instantiated over such
//    a signature — poisons every implementation unit of the module that uses
//    std::format:
//
//        error: satisfaction of atomic constraint
//               'requires{...std::expected<_Tp, _Er>::swap...}' depends on itself
//
//    reported against the std::format call, which has nothing to do with it. The
//    same body returning std::expected<int, Error> or std::expected<void, Error>
//    compiles, so it is the non-trivial value type that trips the constraint
//    machinery through the module boundary. See the note in cup.platform's
//    Http.cppm, where it was first cornered. Declaring in the interface and
//    defining in an implementation unit avoids it entirely — and it is the same
//    shape the other two constraints already want.
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too.
module;
#include <string>
export module cup.scaffold;

// Re-exported because utils::error::Error is the E of every result the partitions
// return.
export import utils.error;

export import :std;
export import :naming;
export import :render;
export import :cmake;
export import :compiler;
export import :releases;
export import :dockerhub;
