package scaffold

import (
	"fmt"
	"os"
	"strings"

	"cup/internal/ui"
)

// GuardStart and GuardEnd delimit the cup-managed compiler-version check inside a
// project's root CMakeLists.txt. They let `cup compiler` find and rewrite the
// block in place without disturbing the rest of the file.
const (
	GuardStart = "# >>> cup:compiler-guard >>>"
	GuardEnd   = "# <<< cup:compiler-guard <<<"
)

// MinCompilers returns cup's default minimum GCC and Clang major versions for a
// C++ standard — the floor baked into a new project's cup.toml. They track the
// oldest release cup expects to build each standard end to end (C++23's
// `import std;` needs GCC 15; named modules on C++20 need GCC 11 / Clang 16).
func MinCompilers(std int) (gcc, clang int) {
	switch {
	case std >= 23:
		return 15, 17
	case std >= 20:
		return 11, 16
	case std >= 17:
		return 7, 5
	case std >= 14:
		return 5, 4
	default:
		return 5, 4
	}
}

// CompilerChoices returns the GCC and Clang major versions selectable as a
// minimum for std, oldest first. Each list starts at the baseline that first
// builds the standard (cup's curated default, MinCompilers) and runs up to the
// newest released major (newestGCC / newestClang, discovered live by
// NewestCompilers), so `cup new` offers only compilers that can build the chosen
// standard, without a hardcoded ceiling that rots as toolchains ship.
func CompilerChoices(std, newestGCC, newestClang int) (gcc, clang []int) {
	baseGCC, baseClang := MinCompilers(std)
	return rangeUp(baseGCC, newestGCC), rangeUp(baseClang, newestClang)
}

// rangeUp lists the integers from..to inclusive, oldest first. If to has fallen
// behind from (a baseline newer than the recorded newest), it yields just from.
func rangeUp(from, to int) []int {
	if to < from {
		to = from
	}
	out := make([]int, 0, to-from+1)
	for v := from; v <= to; v++ {
		out = append(out, v)
	}
	return out
}

// CompilerGuard renders the marker-delimited CMake block that halts a build when
// the active compiler is older than the project's floor. gcc and clang are
// minimum major versions; a zero disables the check for that compiler. The block
// always carries both markers so `cup compiler` can rewrite it even when it
// currently enforces nothing.
func CompilerGuard(gcc, clang int) string {
	var b strings.Builder
	b.WriteString(GuardStart + "\n")
	b.WriteString("# Minimum compiler versions, managed by `cup compiler`. Building with an older\n")
	b.WriteString("# toolchain stops here instead of failing deep in a compile. Change a floor with\n")
	b.WriteString("# `cup compiler set gcc|clang <version>` (docker-verified before it is committed).\n")

	branches := make([]string, 0, 2)
	if gcc > 0 {
		branches = append(branches, guardBranch("GNU", "GCC", "gcc", gcc))
	}
	if clang > 0 {
		branches = append(branches, guardBranch("Clang", "Clang", "clang", clang))
	}
	if len(branches) > 0 {
		b.WriteString("if" + strings.Join(branches, "\nelseif") + "\nendif()\n")
	}
	b.WriteString(GuardEnd)
	return b.String()
}

// guardBranch renders one `(...) message(FATAL_ERROR ...)` clause, shared by the
// leading if() and any trailing elseif(). id is the CMake compiler id, label the
// human name, flag the `cup compiler set` argument.
func guardBranch(id, label, flag string, version int) string {
	return fmt.Sprintf(
		"(CMAKE_CXX_COMPILER_ID STREQUAL \"%s\" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS %d)\n"+
			"  message(FATAL_ERROR \"cup: this project requires %s >= %d (have ${CMAKE_CXX_COMPILER_VERSION}). "+
			"Lower the floor with `cup compiler set %s <version>`.\")",
		id, version, label, version, flag)
}

// ReplaceCompilerGuard rewrites the compiler-guard block in the CMakeLists at
// path to enforce the given minimums, leaving the rest of the file untouched. It
// errors if the file has no guard markers (e.g. a hand-written CMakeLists).
func ReplaceCompilerGuard(root, path string, gcc, clang int) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("cannot update %s: %w", rel(root, path), err)
	}
	content := string(b)
	start := strings.Index(content, GuardStart)
	end := strings.Index(content, GuardEnd)
	if start < 0 || end < 0 || end < start {
		return fmt.Errorf("no cup compiler-guard block in %s (markers %q..%q missing)",
			rel(root, path), GuardStart, GuardEnd)
	}
	end += len(GuardEnd)
	updated := content[:start] + CompilerGuard(gcc, clang) + content[end:]
	if err := os.WriteFile(path, []byte(updated), 0o644); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (compiler floor)", rel(root, path)))
	return nil
}
