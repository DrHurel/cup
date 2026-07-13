package scaffold

import (
	"fmt"
	"os"
	"regexp"
	"sort"
	"strings"

	"cup/internal/ui"
)

// ReadFileLines exposes a file's lines to callers outside this package (e.g. the
// dependency scanner). The bool is false only if the file could not be read.
func ReadFileLines(path string) ([]string, bool) {
	return readLines(path)
}

// RemoveDir deletes a directory tree, ignoring a missing path.
func RemoveDir(path string) {
	_ = os.RemoveAll(path)
}

func readLines(path string) ([]string, bool) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, false
	}
	text := string(b)
	if text == "" {
		return nil, true
	}
	return strings.Split(strings.TrimRight(text, "\n"), "\n"), true
}

func writeLines(path string, lines []string) error {
	if len(lines) == 0 {
		return os.WriteFile(path, []byte(""), 0o644)
	}
	return os.WriteFile(path, []byte(strings.Join(lines, "\n")+"\n"), 0o644)
}

// EnsureLine appends line to a CMakeLists (creating it if absent) unless already
// present, verbatim.
func EnsureLine(root, path, line string) error {
	lines, _ := readLines(path)
	for _, l := range lines {
		if l == line {
			return nil
		}
	}
	lines = append(lines, line)
	if err := writeLines(path, lines); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (+ %s)", rel(root, path), line))
	return nil
}

// EnsureLineBefore inserts line immediately before the first line equal to
// anchor, keeping ordering-sensitive directives correct (e.g. third_party must
// precede src/libs). Falls back to appending if the anchor is absent.
func EnsureLineBefore(root, path, line, anchor string) error {
	lines, _ := readLines(path)
	for _, l := range lines {
		if l == line {
			return nil
		}
	}
	inserted := false
	out := make([]string, 0, len(lines)+1)
	for _, l := range lines {
		if !inserted && strings.TrimSpace(l) == anchor {
			out = append(out, line)
			inserted = true
		}
		out = append(out, l)
	}
	if !inserted {
		out = append(out, line)
	}
	if err := writeLines(path, out); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (+ %s)", rel(root, path), line))
	return nil
}

// RemoveLine deletes every occurrence of the exact line. Returns true if any was
// removed.
func RemoveLine(root, path, line string) (bool, error) {
	lines, ok := readLines(path)
	if !ok {
		return false, nil
	}
	kept := lines[:0:0]
	for _, l := range lines {
		if l != line {
			kept = append(kept, l)
		}
	}
	if len(kept) == len(lines) {
		return false, nil
	}
	if err := writeLines(path, kept); err != nil {
		return false, err
	}
	ui.Removed(fmt.Sprintf("%s  (- %s)", rel(root, path), line))
	return true, nil
}

// RemoveMatchingLine deletes every line whose trimmed text matches pattern at its
// start. Used where the registered line may carry extra arguments.
func RemoveMatchingLine(root, path string, pattern *regexp.Regexp) (bool, error) {
	lines, ok := readLines(path)
	if !ok {
		return false, nil
	}
	kept := lines[:0:0]
	for _, l := range lines {
		if !pattern.MatchString(strings.TrimSpace(l)) {
			kept = append(kept, l)
		}
	}
	if len(kept) == len(lines) {
		return false, nil
	}
	if err := writeLines(path, kept); err != nil {
		return false, err
	}
	ui.Removed(rel(root, path))
	return true, nil
}

// AppendBlock appends a multi-line block unless marker already appears in the
// file, treating a repeat registration as a no-op.
func AppendBlock(root, path, marker, block string) error {
	b, _ := os.ReadFile(path)
	existing := string(b)
	if strings.Contains(existing, marker) {
		ui.Skipped(fmt.Sprintf("%s already declares %s", rel(root, path), marker))
		return nil
	}
	prefix := existing
	if prefix != "" && !strings.HasSuffix(prefix, "\n") {
		prefix += "\n"
	}
	if prefix != "" && !strings.HasSuffix(prefix, "\n\n") {
		prefix += "\n"
	}
	if err := os.WriteFile(path, []byte(prefix+block), 0o644); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (+ %s)", rel(root, path), marker))
	return nil
}

// RemoveFetchContentBlock removes the FetchContent_Declare(name …) …
// FetchContent_MakeAvailable(name) block, the inverse of a cmake-download
// registration. Returns true if a block was removed.
func RemoveFetchContentBlock(root, path, name string) (bool, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return false, nil
	}
	pattern := regexp.MustCompile(
		`\n*FetchContent_Declare\(\s*\n\s*` + regexp.QuoteMeta(name) +
			`\b(?s:.*?)FetchContent_MakeAvailable\(\s*` + regexp.QuoteMeta(name) + `\s*\)[ \t]*\n?`,
	)
	newContent, n := replaceCount(pattern, string(b), "\n")
	if n == 0 {
		return false, nil
	}
	if err := os.WriteFile(path, []byte(newContent), 0o644); err != nil {
		return false, err
	}
	ui.Removed(fmt.Sprintf("%s  (- %s)", rel(root, path), name))
	return true, nil
}

func replaceCount(re *regexp.Regexp, src, repl string) (string, int) {
	n := len(re.FindAllString(src, -1))
	if n == 0 {
		return src, 0
	}
	return re.ReplaceAllString(src, repl), n
}

// AddModuleSource appends filename to a library's FILE_SET CXX_MODULES FILES
// block, matching the indentation of the first entry. Idempotent.
func AddModuleSource(root, path, filename string) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("cannot update %s: not found", rel(root, path))
	}
	content := string(b)
	pattern := regexp.MustCompile(`(FILE_SET\s+CXX_MODULES\s+FILES\n)((?:[ \t]+[^\n]+\n)+)`)
	loc := pattern.FindStringSubmatchIndex(content)
	if loc == nil {
		return fmt.Errorf("cannot find FILE_SET CXX_MODULES block in %s", rel(root, path))
	}
	filesBlock := content[loc[4]:loc[5]]
	for _, l := range strings.Split(filesBlock, "\n") {
		if strings.TrimSpace(l) == filename {
			return nil // already listed
		}
	}
	first := strings.SplitN(filesBlock, "\n", 2)[0]
	indent := first[:len(first)-len(strings.TrimLeft(first, " \t"))]
	newBlock := filesBlock + indent + filename + "\n"
	updated := content[:loc[4]] + newBlock + content[loc[5]:]
	if err := os.WriteFile(path, []byte(updated), 0o644); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (+ %s)", rel(root, path), filename))
	return nil
}

// AddHeaderSource appends filename to a header library's FILE_SET HEADERS FILES
// block, matching the indentation of the first entry. Idempotent. It is the
// header-family analogue of AddModuleSource.
func AddHeaderSource(root, path, filename string) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("cannot update %s: not found", rel(root, path))
	}
	content := string(b)
	pattern := regexp.MustCompile(`(FILE_SET\s+HEADERS(?s:.*?)FILES\n)((?:[ \t]+[^\n]+\n)+)`)
	loc := pattern.FindStringSubmatchIndex(content)
	if loc == nil {
		return fmt.Errorf("cannot find FILE_SET HEADERS block in %s", rel(root, path))
	}
	filesBlock := content[loc[4]:loc[5]]
	for _, l := range strings.Split(filesBlock, "\n") {
		if strings.TrimSpace(l) == filename {
			return nil // already listed
		}
	}
	first := strings.SplitN(filesBlock, "\n", 2)[0]
	indent := first[:len(first)-len(strings.TrimLeft(first, " \t"))]
	newBlock := filesBlock + indent + filename + "\n"
	updated := content[:loc[4]] + newBlock + content[loc[5]:]
	if err := os.WriteFile(path, []byte(updated), 0o644); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (+ %s)", rel(root, path), filename))
	return nil
}

var interfaceKeyword = regexp.MustCompile(`\bINTERFACE\b`)

// EnsureHeaderLibStatic promotes a header library's CMakeLists from an INTERFACE
// library to a STATIC one so it can compile a .cpp source. A header-only lib is
// declared `add_library(<name> INTERFACE)` with INTERFACE-scoped properties;
// adding the first compiled component turns it into `add_library(<name> STATIC)`
// with those properties rescoped to PUBLIC. A no-op if the lib is already STATIC.
func EnsureHeaderLibStatic(root, path, name string) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("cannot update %s: not found", rel(root, path))
	}
	content := string(b)
	interfaceDecl := fmt.Sprintf("add_library(%s INTERFACE)", name)
	if !strings.Contains(content, interfaceDecl) {
		return nil // already STATIC (or not a header lib we manage)
	}
	content = strings.Replace(content, interfaceDecl, fmt.Sprintf("add_library(%s STATIC)", name), 1)
	content = interfaceKeyword.ReplaceAllString(content, "PUBLIC")
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (INTERFACE -> STATIC)", rel(root, path)))
	return nil
}

// AddPartitionImport re-exports a module partition from its lib's primary module
// interface unit, inserting `export import :<partition>;` — appended to the
// existing block of partition imports, or as a fresh block right after the
// `export module` declaration. Idempotent.
func AddPartitionImport(root, primary, partition string) error {
	b, err := os.ReadFile(primary)
	if err != nil {
		return fmt.Errorf("cannot update %s: not found", rel(root, primary))
	}
	directive := fmt.Sprintf("export import :%s;", partition)
	lines := strings.Split(strings.TrimRight(string(b), "\n"), "\n")
	for _, l := range lines {
		if l == directive {
			return nil
		}
	}
	out, err := insertPartitionImport(lines, directive)
	if err != nil {
		return fmt.Errorf("%w in %s", err, rel(root, primary))
	}
	if err := os.WriteFile(primary, []byte(strings.Join(out, "\n")+"\n"), 0o644); err != nil {
		return err
	}
	ui.Updated(fmt.Sprintf("%s  (+ %s)", rel(root, primary), directive))
	return nil
}

// insertPartitionImport returns lines with directive inserted after the last
// existing `export import :` line, or — if there are none — as a fresh block
// right after the `export module` declaration. It errors if no module
// declaration is present.
func insertPartitionImport(lines []string, directive string) ([]string, error) {
	lastImport := -1
	for i, l := range lines {
		if strings.HasPrefix(l, "export import :") {
			lastImport = i
		}
	}
	if lastImport >= 0 {
		out := append([]string{}, lines[:lastImport+1]...)
		out = append(out, directive)
		return append(out, lines[lastImport+1:]...), nil
	}
	for i, l := range lines {
		if strings.HasPrefix(l, "export module ") {
			out := append([]string{}, lines[:i+1]...)
			out = append(out, "", directive)
			return append(out, lines[i+1:]...), nil
		}
	}
	return nil, fmt.Errorf("cannot find module declaration")
}

// ListSubdirs returns the sorted names of immediate subdirectories of path.
func ListSubdirs(path string) []string {
	entries, err := os.ReadDir(path)
	if err != nil {
		return nil
	}
	var dirs []string
	for _, e := range entries {
		if e.IsDir() {
			dirs = append(dirs, e.Name())
		}
	}
	sort.Strings(dirs)
	return dirs
}
