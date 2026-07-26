package cmd

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"testing"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/tmpl"
)

// The golden tree corpus is the migration's behavioural spec: it pins the *whole*
// tree `cup new` produces for every (build tool × standard) cell, so the C++
// rewrite can be checked against the same files rather than against a
// reimplementation of the assertions. Regenerate after an intentional change with:
//
//	go test ./internal/cmd -update
//
// and review the diff — every line of it is a user-visible change to what cup
// scaffolds.
var update = flag.Bool("update", false, "rewrite the golden trees under testdata/golden/")

// goldenDir is resolved at package load, before any test can t.Chdir into a temp
// project. Package-level initialisation runs in the package directory, so this is
// the only point at which "testdata" reliably means *this* package's testdata.
var goldenDir = func() string {
	wd, err := os.Getwd()
	if err != nil {
		panic("cannot resolve testdata directory: " + err.Error())
	}
	return filepath.Join(wd, "testdata", "golden")
}()

// goldenNewestGCC and goldenNewestClang pin the release ceiling the compiler
// pickers offer, so the golden trees never depend on what has shipped since (or on
// reaching the network). They only cap the *choices*; each cell selects the oldest
// offered floor, which is cup's curated baseline for that standard.
const (
	goldenNewestGCC   = 15
	goldenNewestClang = 20
)

// --- tree serialisation -----------------------------------------------------

// cupVersionLine matches cup.toml's stamped cup_version so the goldens survive a
// release bump. It is the only value in a scaffolded tree that changes without a
// behavioural change.
var cupVersionLine = regexp.MustCompile(`(?m)^(\s*cup_version\s*=\s*)"[^"]*"$`)

// snapshotTree renders the directory at root as a single deterministic text
// document — the format both implementations must agree on.
//
// Entries are sorted by slash-separated path, so the manifest is stable across
// filesystems. Every directory is listed (empty ones carry meaning: the Make
// backend creates src/apps and src/libs with no file in them), and every file is
// listed with its byte count, which keeps the record unambiguous when file content
// itself contains a line that looks like a header.
//
//	dir  <path>/
//	file <path> (<n> bytes)
//	>| <line>
//	…
//
// Content lines are prefixed with ">| " so file text can never be confused with
// the manifest's own structure. A file whose mode carries any execute bit is
// marked `file <path> (<n> bytes, exec)`.
func snapshotTree(t *testing.T, root string) string {
	t.Helper()

	entries, err := collectTree(root)
	if err != nil {
		t.Fatalf("walking %s: %v", root, err)
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].path < entries[j].path })

	var b strings.Builder
	for _, e := range entries {
		writeTreeEntry(&b, e)
	}

	// A scaffolded tree must never embed the (temp) directory it happens to live
	// in; if it does, the golden is unreproducible and the leak is a real bug.
	out := b.String()
	if strings.Contains(out, root) {
		t.Fatalf("generated tree embeds its absolute root path %q", root)
	}
	return out
}

// treeEntry is one file or directory of a snapshot, keyed by its slash-separated
// path relative to the project root.
type treeEntry struct {
	path  string
	isDir bool
	mode  os.FileMode
	body  []byte
}

// collectTree reads every file and directory under root into treeEntry values.
func collectTree(root string) ([]treeEntry, error) {
	var entries []treeEntry
	err := filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(root, path)
		if err != nil || rel == "." {
			return err
		}
		info, err := d.Info()
		if err != nil {
			return err
		}
		e := treeEntry{path: filepath.ToSlash(rel), isDir: d.IsDir(), mode: info.Mode()}
		if !d.IsDir() {
			if e.body, err = os.ReadFile(path); err != nil {
				return err
			}
		}
		entries = append(entries, e)
		return nil
	})
	return entries, err
}

// writeTreeEntry renders one entry in the manifest format described on
// snapshotTree.
func writeTreeEntry(b *strings.Builder, e treeEntry) {
	if e.isDir {
		fmt.Fprintf(b, "dir  %s/\n", e.path)
		return
	}
	body := cupVersionLine.ReplaceAllString(string(e.body), `${1}"{{CUP_VERSION}}"`)
	exec := ""
	if e.mode&0o111 != 0 {
		exec = ", exec"
	}
	fmt.Fprintf(b, "file %s (%d bytes%s)\n", e.path, len(body), exec)
	for _, line := range strings.Split(body, "\n") {
		fmt.Fprintf(b, ">| %s\n", line)
	}
}

// assertGolden compares got against testdata/golden/<name>.txt, or rewrites it
// under -update.
func assertGolden(t *testing.T, name, got string) {
	t.Helper()
	path := filepath.Join(goldenDir, name+".txt")

	if *update {
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
		}
		if err := os.WriteFile(path, []byte(got), 0o644); err != nil {
			t.Fatalf("writing golden %s: %v", path, err)
		}
		t.Logf("updated %s", path)
		return
	}

	want, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("reading golden %s: %v\nrun `go test ./internal/cmd -update` to create it", path, err)
	}
	if string(want) != got {
		t.Errorf("generated tree differs from %s\n%s", path, firstDiff(string(want), got))
	}
}

// firstDiff reports the first differing line between want and got, which is far
// more usable than dumping two multi-thousand-line trees.
func firstDiff(want, got string) string {
	w, g := strings.Split(want, "\n"), strings.Split(got, "\n")
	for i := 0; i < len(w) || i < len(g); i++ {
		lw, lg := "", ""
		if i < len(w) {
			lw = w[i]
		}
		if i < len(g) {
			lg = g[i]
		}
		if lw != lg {
			return fmt.Sprintf("first difference at line %d:\n  golden: %q\n  actual: %q", i+1, lw, lg)
		}
	}
	return "(files differ but no differing line found)"
}

// --- `cup new` matrix -------------------------------------------------------

// newFeed builds the exact prompt script `cup new <name>` consumes for a cell.
// The compiler-floor prompts are conditional: chooseCompilerFloor takes a lone
// option without reading input, which happens for GCC on C++23 (15 is both the
// baseline and the pinned ceiling).
func newFeed(t *testing.T, tool string, std int) string {
	t.Helper()

	toolIdx := 1
	if tool == project.ToolMake {
		toolIdx = 2
	}

	stds := standardChoices(tool)
	stdIdx := 0
	for i, s := range stds {
		if s == std {
			stdIdx = i + 1
		}
	}
	if stdIdx == 0 {
		t.Fatalf("standard c++%d is not offered for build tool %q", std, tool)
	}

	// build tool, standard, then "gcc and clang" for the floor question.
	lines := []string{strconv.Itoa(toolIdx), strconv.Itoa(stdIdx), "1"}

	gccChoices, clangChoices := scaffold.CompilerChoices(std, goldenNewestGCC, goldenNewestClang)
	for _, choices := range [][]int{gccChoices, clangChoices} {
		if len(choices) > 1 {
			lines = append(lines, "1") // oldest offered floor = cup's baseline
		}
	}

	// base image: repository, then the first stubbed tag.
	lines = append(lines, "gcc", "1")
	return strings.Join(lines, "\n") + "\n"
}

// goldenCell is one (build tool × standard) point of the matrix. CMake drives
// every standard; Make is offered only the headers family, so the two axes are
// not a full cross product.
type goldenCell struct {
	tool string
	std  int
}

func (c goldenCell) name() string { return fmt.Sprintf("%s-cpp%d", c.tool, c.std) }

var goldenCells = []goldenCell{
	{project.ToolCMake, 23},
	{project.ToolCMake, 20},
	{project.ToolCMake, 17},
	{project.ToolCMake, 14},
	{project.ToolCMake, 11},
	{project.ToolMake, 17},
	{project.ToolMake, 14},
	{project.ToolMake, 11},
}

// pinScaffoldEnv makes a cell reproducible: a fixed compiler-release ceiling, a
// fixed Docker Hub tag list, and a stubbed runCommand so `git init` never plants a
// non-deterministic .git/ in the snapshot.
func pinScaffoldEnv(t *testing.T) {
	t.Helper()
	restore := scaffold.NewestCompilersFunc
	scaffold.NewestCompilersFunc = func() (int, int) { return goldenNewestGCC, goldenNewestClang }
	t.Cleanup(func() { scaffold.NewestCompilersFunc = restore })
	withStubTags(t, []string{"14", "13", "12"}, nil)
	stubRunCommand(t, nil)
}

// TestGoldenNew pins the full tree `cup new` scaffolds across the matrix.
func TestGoldenNew(t *testing.T) {
	for _, c := range goldenCells {
		t.Run(c.name(), func(t *testing.T) {
			pinScaffoldEnv(t)
			root := scaffoldProject(t, c)
			assertGolden(t, filepath.Join("new", c.name()), snapshotTree(t, root))
		})
	}
}

// scaffoldProject runs `cup new demo` for a cell inside a fresh temp directory and
// returns the project root, leaving the test's working directory inside it so the
// `cup add` flows (which locate the project by walking up from the cwd) can run.
func scaffoldProject(t *testing.T, c goldenCell) string {
	t.Helper()
	dir := t.TempDir()
	t.Chdir(dir)
	feed(t, newFeed(t, c.tool, c.std))

	if err := RunNew([]string{"demo"}); err != nil {
		t.Fatalf("RunNew(%s, c++%d): %v", c.tool, c.std, err)
	}

	// The chosen standard and tool must actually be what landed in cup.toml, or the
	// golden would faithfully record the wrong cell.
	root := filepath.Join(dir, "demo")
	assertConfig(t, root, c.tool, c.std)
	t.Chdir(root)
	return root
}

// --- `cup add` lifecycle ----------------------------------------------------

// TestGoldenAdd pins what a project looks like after it has been grown through the
// add flows: an app, one library per available component kind, and a test bound to
// the first of them. Between them these cover every component template in the
// family, plus the registration side effects — add_subdirectory lines appended to
// the parent CMakeLists, module sources and partition imports wired into a lib's
// own files — that no single-file assertion captures well.
func TestGoldenAdd(t *testing.T) {
	for _, c := range goldenCells {
		t.Run(c.name(), func(t *testing.T) {
			pinScaffoldEnv(t)
			root := scaffoldProject(t, c)

			// app: name, then accept the default source filename.
			feed(t, "runner\n\n")
			if err := RunAdd([]string{"app"}); err != nil {
				t.Fatalf("cup add app: %v", err)
			}

			// One library per kind, in picker order — so the nth library is also the
			// nth kind, which is what addLibFeed relies on.
			for i, kind := range tmpl.Kinds(root, scaffold.Family(c.std)) {
				feed(t, addLibFeed(i, libNameFor(kind)))
				if err := RunAdd([]string{"lib"}); err != nil {
					t.Fatalf("cup add lib (%s): %v", kind, err)
				}
			}

			// test: name, then "2" — option 1 is [none], so this binds the test to the
			// first library, exercising the module-under-test wiring.
			feed(t, "smoke\n2\n")
			if err := RunAdd([]string{"test"}); err != nil {
				t.Fatalf("cup add test: %v", err)
			}

			assertGolden(t, filepath.Join("add", c.name()), snapshotTree(t, root))
		})
	}
}

// libNameFor turns a template kind into a valid C++ identifier to name its library
// after, so each golden lib says which kind produced it (free-function ->
// lib_free_function).
func libNameFor(kind string) string {
	return "lib_" + strings.ReplaceAll(kind, "-", "_")
}

// addLibFeed scripts `cup add lib` for the nth library added to a project (n is
// 0-based, and equals both the number of libraries already present and — since the
// caller walks the kinds in picker order — the index of the kind to select).
//
// The first library finds an empty src/libs and is prompted for a name outright;
// every later one gets a picker over the existing libraries plus a trailing
// "[new…]" entry, which sits at position n+1. Then comes the template kind and the
// primary symbol name, whose default (the capitalised lib name) is accepted.
func addLibFeed(n int, name string) string {
	var lines []string
	if n > 0 {
		lines = append(lines, strconv.Itoa(n+1)) // the "[new…]" entry
	}
	lines = append(lines, name, strconv.Itoa(n+1), "")
	return strings.Join(lines, "\n") + "\n"
}

// assertConfig fails unless the scaffolded cup.toml records the tool and standard
// the feed was meant to select — a guard against a silently mis-scripted prompt
// sequence baking a wrong tree into the goldens.
func assertConfig(t *testing.T, root, tool string, std int) {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(root, project.Marker))
	if err != nil {
		t.Fatalf("reading %s: %v", project.Marker, err)
	}
	got := string(b)
	if want := fmt.Sprintf("cpp_standard = %d", std); !strings.Contains(got, want) {
		t.Fatalf("cup.toml missing %q:\n%s", want, got)
	}
	if tool == project.ToolMake && !strings.Contains(got, `build_tool = "make"`) {
		t.Fatalf("cup.toml missing make build_tool:\n%s", got)
	}
	if tool == project.ToolCMake && strings.Contains(got, `build_tool = "make"`) {
		t.Fatalf("cup.toml records make for a cmake cell:\n%s", got)
	}
}
