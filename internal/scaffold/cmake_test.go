package scaffold

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// writeFile writes content to root/name (creating parent dirs) and returns the
// full path.
func writeFile(t *testing.T, root, name, content string) string {
	t.Helper()
	path := filepath.Join(root, name)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func read(t *testing.T, path string) string {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(b)
}

func TestReadFileLines(t *testing.T) {
	root := t.TempDir()

	// Missing file: ok is false.
	if _, ok := ReadFileLines(filepath.Join(root, "nope")); ok {
		t.Error("ReadFileLines(missing) ok = true, want false")
	}

	// Empty file: ok true, no lines.
	empty := writeFile(t, root, "empty", "")
	lines, ok := ReadFileLines(empty)
	if !ok || len(lines) != 0 {
		t.Errorf("ReadFileLines(empty) = %v, %v, want [], true", lines, ok)
	}

	// Trailing newline is trimmed, so no phantom empty final line.
	f := writeFile(t, root, "f", "a\nb\nc\n")
	lines, ok = ReadFileLines(f)
	if !ok {
		t.Fatal("ReadFileLines ok = false, want true")
	}
	if want := []string{"a", "b", "c"}; !equalLines(lines, want) {
		t.Errorf("ReadFileLines = %v, want %v", lines, want)
	}
}

func TestRemoveDir(t *testing.T) {
	root := t.TempDir()
	dir := filepath.Join(root, "tree", "sub")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	RemoveDir(filepath.Join(root, "tree"))
	if _, err := os.Stat(filepath.Join(root, "tree")); !os.IsNotExist(err) {
		t.Error("RemoveDir did not remove the tree")
	}
	// Missing path is ignored (no panic / error surfaced).
	RemoveDir(filepath.Join(root, "does-not-exist"))
}

func TestEnsureLine(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "CMakeLists.txt")

	// Creates the file when absent.
	if err := EnsureLine(root, path, "add_subdirectory(src)"); err != nil {
		t.Fatalf("EnsureLine (create) error: %v", err)
	}
	if read(t, path) != "add_subdirectory(src)\n" {
		t.Errorf("after create = %q", read(t, path))
	}

	// Appends a distinct line.
	if err := EnsureLine(root, path, "add_subdirectory(tests)"); err != nil {
		t.Fatalf("EnsureLine (append) error: %v", err)
	}
	if want := "add_subdirectory(src)\nadd_subdirectory(tests)\n"; read(t, path) != want {
		t.Errorf("after append = %q, want %q", read(t, path), want)
	}

	// Idempotent: an existing line is not duplicated.
	if err := EnsureLine(root, path, "add_subdirectory(src)"); err != nil {
		t.Fatalf("EnsureLine (dup) error: %v", err)
	}
	if strings.Count(read(t, path), "add_subdirectory(src)") != 1 {
		t.Errorf("EnsureLine duplicated a line: %q", read(t, path))
	}
}

func TestEnsureLineBefore(t *testing.T) {
	root := t.TempDir()

	// Inserts immediately before the anchor.
	path := writeFile(t, root, "CMakeLists.txt", "add_subdirectory(src)\nadd_subdirectory(libs)\n")
	if err := EnsureLineBefore(root, path, "add_subdirectory(third_party)", "add_subdirectory(src)"); err != nil {
		t.Fatalf("EnsureLineBefore error: %v", err)
	}
	want := "add_subdirectory(third_party)\nadd_subdirectory(src)\nadd_subdirectory(libs)\n"
	if read(t, path) != want {
		t.Errorf("insert-before = %q, want %q", read(t, path), want)
	}

	// Idempotent.
	if err := EnsureLineBefore(root, path, "add_subdirectory(third_party)", "add_subdirectory(src)"); err != nil {
		t.Fatalf("EnsureLineBefore (dup) error: %v", err)
	}
	if strings.Count(read(t, path), "third_party") != 1 {
		t.Errorf("EnsureLineBefore duplicated: %q", read(t, path))
	}

	// Missing anchor falls back to appending.
	path2 := writeFile(t, root, "other.txt", "line1\n")
	if err := EnsureLineBefore(root, path2, "inserted", "no-such-anchor"); err != nil {
		t.Fatalf("EnsureLineBefore (no anchor) error: %v", err)
	}
	if want := "line1\ninserted\n"; read(t, path2) != want {
		t.Errorf("no-anchor fallback = %q, want %q", read(t, path2), want)
	}
}

func TestRemoveLine(t *testing.T) {
	root := t.TempDir()
	path := writeFile(t, root, "CMakeLists.txt", "keep\ndrop\nkeep\ndrop\n")

	removed, err := RemoveLine(root, path, "drop")
	if err != nil {
		t.Fatalf("RemoveLine error: %v", err)
	}
	if !removed {
		t.Error("RemoveLine returned false, want true")
	}
	if want := "keep\nkeep\n"; read(t, path) != want {
		t.Errorf("after removal = %q, want %q", read(t, path), want)
	}

	// Removing an absent line reports false and leaves the file unchanged.
	removed, err = RemoveLine(root, path, "drop")
	if err != nil {
		t.Fatalf("RemoveLine (absent) error: %v", err)
	}
	if removed {
		t.Error("RemoveLine(absent) returned true, want false")
	}

	// Missing file reports false, no error.
	removed, err = RemoveLine(root, filepath.Join(root, "nope"), "x")
	if err != nil || removed {
		t.Errorf("RemoveLine(missing) = %v, %v, want false, nil", removed, err)
	}
}

func TestRemoveMatchingLine(t *testing.T) {
	root := t.TempDir()
	path := writeFile(t, root, "CMakeLists.txt", "add_dep(foo v1)\nkeep\n  add_dep(foo v2)\n")

	pattern := regexp.MustCompile(`^add_dep\(foo\b`)
	removed, err := RemoveMatchingLine(root, path, pattern)
	if err != nil {
		t.Fatalf("RemoveMatchingLine error: %v", err)
	}
	if !removed {
		t.Error("RemoveMatchingLine returned false, want true")
	}
	// Both matching lines gone (leading indent is trimmed before matching), keep stays.
	if want := "keep\n"; read(t, path) != want {
		t.Errorf("after removal = %q, want %q", read(t, path), want)
	}

	// No match: false, unchanged.
	removed, _ = RemoveMatchingLine(root, path, regexp.MustCompile(`^zzz`))
	if removed {
		t.Error("RemoveMatchingLine(no match) returned true, want false")
	}

	// Missing file: false, no error.
	removed, err = RemoveMatchingLine(root, filepath.Join(root, "nope"), pattern)
	if err != nil || removed {
		t.Errorf("RemoveMatchingLine(missing) = %v, %v, want false, nil", removed, err)
	}
}

func TestAppendBlock(t *testing.T) {
	root := t.TempDir()
	path := writeFile(t, root, "CMakeLists.txt", "project(demo)")

	block := "FetchContent_Declare(\n  fmt\n)\n"
	if err := AppendBlock(root, path, "fmt", block); err != nil {
		t.Fatalf("AppendBlock error: %v", err)
	}
	got := read(t, path)
	if !strings.Contains(got, "FetchContent_Declare") {
		t.Errorf("AppendBlock did not add the block: %q", got)
	}
	// A blank line separates the prior content from the appended block.
	if !strings.Contains(got, "project(demo)\n\nFetchContent_Declare") {
		t.Errorf("AppendBlock spacing wrong: %q", got)
	}

	// Marker already present: no-op.
	before := read(t, path)
	if err := AppendBlock(root, path, "fmt", block); err != nil {
		t.Fatalf("AppendBlock (dup) error: %v", err)
	}
	if read(t, path) != before {
		t.Errorf("AppendBlock re-added an existing marker: %q", read(t, path))
	}
}

func TestAppendBlockNewFile(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "CMakeLists.txt")
	// Nonexistent file: prefix is empty, block written verbatim.
	if err := AppendBlock(root, path, "m", "block\n"); err != nil {
		t.Fatalf("AppendBlock (new) error: %v", err)
	}
	if read(t, path) != "block\n" {
		t.Errorf("AppendBlock(new) = %q, want %q", read(t, path), "block\n")
	}
}

func TestRemoveFetchContentBlock(t *testing.T) {
	root := t.TempDir()
	content := `project(demo)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
)
FetchContent_MakeAvailable(fmt)

add_subdirectory(src)
`
	path := writeFile(t, root, "CMakeLists.txt", content)

	removed, err := RemoveFetchContentBlock(root, path, "fmt")
	if err != nil {
		t.Fatalf("RemoveFetchContentBlock error: %v", err)
	}
	if !removed {
		t.Error("RemoveFetchContentBlock returned false, want true")
	}
	got := read(t, path)
	if strings.Contains(got, "FetchContent_Declare") || strings.Contains(got, "fmt") {
		t.Errorf("block not fully removed: %q", got)
	}
	if !strings.Contains(got, "project(demo)") || !strings.Contains(got, "add_subdirectory(src)") {
		t.Errorf("surrounding content lost: %q", got)
	}

	// Absent name: false.
	removed, _ = RemoveFetchContentBlock(root, path, "absent")
	if removed {
		t.Error("RemoveFetchContentBlock(absent) returned true, want false")
	}

	// Missing file: false, no error.
	removed, err = RemoveFetchContentBlock(root, filepath.Join(root, "nope"), "fmt")
	if err != nil || removed {
		t.Errorf("RemoveFetchContentBlock(missing) = %v, %v, want false, nil", removed, err)
	}
}

func TestAddModuleSource(t *testing.T) {
	root := t.TempDir()
	content := `add_library(mylib)
target_sources(mylib
  PUBLIC
  FILE_SET CXX_MODULES FILES
    mylib.cppm
)
`
	path := writeFile(t, root, "CMakeLists.txt", content)

	if err := AddModuleSource(root, path, "extra.cppm"); err != nil {
		t.Fatalf("AddModuleSource error: %v", err)
	}
	got := read(t, path)
	if !strings.Contains(got, "extra.cppm") {
		t.Errorf("AddModuleSource did not add the file: %q", got)
	}
	// Indentation matches the first entry (4 spaces).
	if !strings.Contains(got, "    extra.cppm") {
		t.Errorf("AddModuleSource wrong indent: %q", got)
	}

	// Idempotent.
	if err := AddModuleSource(root, path, "extra.cppm"); err != nil {
		t.Fatalf("AddModuleSource (dup) error: %v", err)
	}
	if strings.Count(read(t, path), "extra.cppm") != 1 {
		t.Errorf("AddModuleSource duplicated: %q", read(t, path))
	}
}

func TestAddModuleSourceErrors(t *testing.T) {
	root := t.TempDir()

	// Missing file.
	if err := AddModuleSource(root, filepath.Join(root, "nope"), "x.cppm"); err == nil {
		t.Error("AddModuleSource(missing) = nil error, want error")
	}

	// No FILE_SET block.
	path := writeFile(t, root, "CMakeLists.txt", "add_library(mylib)\n")
	if err := AddModuleSource(root, path, "x.cppm"); err == nil {
		t.Error("AddModuleSource(no block) = nil error, want error")
	}
}

func TestAddHeaderSource(t *testing.T) {
	root := t.TempDir()
	content := `add_library(mylib INTERFACE)
target_sources(mylib
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS include
  FILES
    mylib/a.hpp
)
`
	path := writeFile(t, root, "CMakeLists.txt", content)

	if err := AddHeaderSource(root, path, "mylib/b.hpp"); err != nil {
		t.Fatalf("AddHeaderSource error: %v", err)
	}
	got := read(t, path)
	if !strings.Contains(got, "    mylib/b.hpp") {
		t.Errorf("AddHeaderSource did not add with matching indent: %q", got)
	}

	// Idempotent.
	if err := AddHeaderSource(root, path, "mylib/b.hpp"); err != nil {
		t.Fatalf("AddHeaderSource (dup) error: %v", err)
	}
	if strings.Count(read(t, path), "mylib/b.hpp") != 1 {
		t.Errorf("AddHeaderSource duplicated: %q", read(t, path))
	}
}

func TestAddHeaderSourceErrors(t *testing.T) {
	root := t.TempDir()
	if err := AddHeaderSource(root, filepath.Join(root, "nope"), "x.hpp"); err == nil {
		t.Error("AddHeaderSource(missing) = nil error, want error")
	}
	path := writeFile(t, root, "CMakeLists.txt", "add_library(mylib)\n")
	if err := AddHeaderSource(root, path, "x.hpp"); err == nil {
		t.Error("AddHeaderSource(no block) = nil error, want error")
	}
}

func TestEnsureHeaderLibStatic(t *testing.T) {
	root := t.TempDir()
	content := `add_library(mylib INTERFACE)
target_include_directories(mylib INTERFACE include)
target_compile_features(mylib INTERFACE cxx_std_20)
`
	path := writeFile(t, root, "CMakeLists.txt", content)

	if err := EnsureHeaderLibStatic(root, path, "mylib"); err != nil {
		t.Fatalf("EnsureHeaderLibStatic error: %v", err)
	}
	got := read(t, path)
	if !strings.Contains(got, "add_library(mylib STATIC)") {
		t.Errorf("declaration not promoted to STATIC: %q", got)
	}
	if strings.Contains(got, "INTERFACE") {
		t.Errorf("INTERFACE keyword not rescoped to PUBLIC: %q", got)
	}
	if strings.Count(got, "PUBLIC") != 2 {
		t.Errorf("expected 2 PUBLIC scopes, got: %q", got)
	}

	// Already STATIC: no-op.
	before := read(t, path)
	if err := EnsureHeaderLibStatic(root, path, "mylib"); err != nil {
		t.Fatalf("EnsureHeaderLibStatic (noop) error: %v", err)
	}
	if read(t, path) != before {
		t.Errorf("EnsureHeaderLibStatic changed an already-STATIC lib: %q", read(t, path))
	}
}

func TestEnsureHeaderLibStaticMissing(t *testing.T) {
	root := t.TempDir()
	if err := EnsureHeaderLibStatic(root, filepath.Join(root, "nope"), "mylib"); err == nil {
		t.Error("EnsureHeaderLibStatic(missing) = nil error, want error")
	}
}

func TestAddPartitionImport(t *testing.T) {
	root := t.TempDir()

	// Fresh block: inserted after the module declaration with a blank line.
	primary := writeFile(t, root, "mylib.cppm", "export module mylib;\n\nint x;\n")
	if err := AddPartitionImport(root, primary, "parser"); err != nil {
		t.Fatalf("AddPartitionImport error: %v", err)
	}
	got := read(t, primary)
	if !strings.Contains(got, "export import :parser;") {
		t.Errorf("partition import not added: %q", got)
	}
	if !strings.Contains(got, "export module mylib;\n\nexport import :parser;") {
		t.Errorf("fresh block placement wrong: %q", got)
	}

	// Second partition: appended after the last existing import.
	if err := AddPartitionImport(root, primary, "lexer"); err != nil {
		t.Fatalf("AddPartitionImport (second) error: %v", err)
	}
	got = read(t, primary)
	if !strings.Contains(got, "export import :parser;\nexport import :lexer;") {
		t.Errorf("second import not appended after first: %q", got)
	}

	// Idempotent.
	if err := AddPartitionImport(root, primary, "parser"); err != nil {
		t.Fatalf("AddPartitionImport (dup) error: %v", err)
	}
	if strings.Count(read(t, primary), ":parser;") != 1 {
		t.Errorf("AddPartitionImport duplicated: %q", read(t, primary))
	}
}

func TestAddPartitionImportErrors(t *testing.T) {
	root := t.TempDir()

	// Missing file.
	if err := AddPartitionImport(root, filepath.Join(root, "nope"), "p"); err == nil {
		t.Error("AddPartitionImport(missing) = nil error, want error")
	}

	// No module declaration.
	primary := writeFile(t, root, "bad.cppm", "int x;\n")
	if err := AddPartitionImport(root, primary, "p"); err == nil {
		t.Error("AddPartitionImport(no module decl) = nil error, want error")
	}
}

func TestListSubdirs(t *testing.T) {
	root := t.TempDir()
	for _, d := range []string{"zeta", "alpha", "mid"} {
		if err := os.MkdirAll(filepath.Join(root, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	writeFile(t, root, "file.txt", "x") // a regular file must be ignored

	got := ListSubdirs(root)
	want := []string{"alpha", "mid", "zeta"} // sorted
	if !equalLines(got, want) {
		t.Errorf("ListSubdirs = %v, want %v", got, want)
	}

	// Missing directory returns nil.
	if got := ListSubdirs(filepath.Join(root, "nope")); got != nil {
		t.Errorf("ListSubdirs(missing) = %v, want nil", got)
	}
}

func equalLines(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
