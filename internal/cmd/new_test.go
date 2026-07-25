package cmd

import (
	"path/filepath"
	"strings"
	"testing"

	"cup/internal/project"
	"cup/internal/scaffold"
)

func TestResolveProjectName(t *testing.T) {
	// From args, valid.
	got, err := resolveProjectName([]string{"widget"})
	if err != nil || got != "widget" {
		t.Fatalf("resolveProjectName(args) = %q, %v", got, err)
	}
	// From args, invalid identifier.
	if _, err := resolveProjectName([]string{"1bad"}); err == nil {
		t.Error("resolveProjectName(1bad) = nil error, want error")
	}
	// From prompt.
	feed(t, "fromprompt\n")
	got, err = resolveProjectName(nil)
	if err != nil || got != "fromprompt" {
		t.Fatalf("resolveProjectName(prompt) = %q, %v", got, err)
	}
}

func TestChooseStandard(t *testing.T) {
	feed(t, "1\n") // first label is the newest standard
	std, err := chooseStandard(project.ToolCMake)
	if err != nil || std < 11 {
		t.Fatalf("chooseStandard = %d, %v", std, err)
	}
}

func TestStandardChoices(t *testing.T) {
	// CMake offers every standard; Make only the headers family (below C++20).
	if got := standardChoices(project.ToolCMake); len(got) != len(scaffold.Standards) {
		t.Errorf("standardChoices(cmake) = %v, want all standards", got)
	}
	for _, s := range standardChoices(project.ToolMake) {
		if scaffold.UsesModules(s) {
			t.Errorf("standardChoices(make) offers modules standard %d", s)
		}
	}
	if len(standardChoices(project.ToolMake)) == 0 {
		t.Error("standardChoices(make) is empty")
	}
}

func TestChooseStandardMakeGated(t *testing.T) {
	// For Make the first option is the newest headers standard (C++17), never a
	// modules one.
	feed(t, "1\n")
	std, err := chooseStandard(project.ToolMake)
	if err != nil || std != 17 {
		t.Fatalf("chooseStandard(make) = %d, %v, want 17", std, err)
	}
}

func TestChooseCompilerFloorSingle(t *testing.T) {
	// A lone choice is taken without reading any input (no feed set up here).
	got, err := chooseCompilerFloor("gcc", []int{15})
	if err != nil || got != 15 {
		t.Fatalf("chooseCompilerFloor(single) = %d, %v, want 15", got, err)
	}
}

func TestChooseCompilerFloorPrompt(t *testing.T) {
	feed(t, "2\n") // second option
	got, err := chooseCompilerFloor("clang", []int{17, 18, 19, 20})
	if err != nil || got != 18 {
		t.Fatalf("chooseCompilerFloor(prompt) = %d, %v, want 18", got, err)
	}
}

func TestChooseCompilerFloorsSubset(t *testing.T) {
	restore := scaffold.NewestCompilersFunc
	scaffold.NewestCompilersFunc = func() (int, int) { return 15, 20 }
	t.Cleanup(func() { scaffold.NewestCompilersFunc = restore })

	// "gcc only" (option 2): prompt gcc, leave clang unpinned. For C++20 gcc
	// offers 11..15; option 1 = 11.
	feed(t, "2\n1\n")
	gcc, clang, err := chooseCompilerFloors(20)
	if err != nil || gcc != 11 || clang != 0 {
		t.Fatalf("gcc only = (%d, %d), %v; want (11, 0)", gcc, clang, err)
	}

	// "clang only" (option 3): prompt clang, leave gcc unpinned. Clang offers
	// 16..20; option 2 = 17.
	feed(t, "3\n2\n")
	gcc, clang, err = chooseCompilerFloors(20)
	if err != nil || gcc != 0 || clang != 17 {
		t.Fatalf("clang only = (%d, %d), %v; want (0, 17)", gcc, clang, err)
	}
}

func TestModuleStdSetup(t *testing.T) {
	if !strings.Contains(moduleStdSetup(23), "CMAKE_CXX_MODULE_STD") {
		t.Error("c++23 setup missing std-module opt-in")
	}
	if !strings.Contains(moduleStdSetup(20), "3.28") {
		t.Error("c++20 setup missing 3.28 floor")
	}
	if strings.Contains(moduleStdSetup(20), "CMAKE_CXX_MODULE_STD") {
		t.Error("c++20 should not enable the std module")
	}
}

// RunNew bootstraps a whole project (it shells out to `git init`, which is
// available in CI). Drive it end-to-end in a temp working directory.
func TestRunNewEndToEnd(t *testing.T) {
	// Pin the release ceiling so the picker is deterministic and never hits the
	// network (C++23 -> GCC sole option 15, Clang 17..20).
	restore := scaffold.NewestCompilersFunc
	scaffold.NewestCompilersFunc = func() (int, int) { return 15, 20 }
	t.Cleanup(func() { scaffold.NewestCompilersFunc = restore })
	withStubTags(t, []string{"14", "13"}, nil)

	dir := t.TempDir()
	t.Chdir(dir)
	// build-tool Select: option 1 (cmake). standard Select: option 1 (C++23).
	// "which compilers" Select: option 1 (both). GCC has a single valid floor (15)
	// so it is auto-chosen; Clang Select: option 1 (its baseline). Base image: repo
	// "gcc", tag Select option 1 (14). name arg.
	feed(t, "1\n1\n1\n1\ngcc\n1\n")
	if err := RunNew([]string{"proj"}); err != nil {
		t.Fatalf("RunNew: %v", err)
	}
	root := filepath.Join(dir, "proj")
	assertFile(t, filepath.Join(root, "cup.toml"), "proj")
	assertFile(t, filepath.Join(root, "CMakeLists.txt"), "proj")
	assertFile(t, filepath.Join(root, ".gitignore"), "")
	assertFile(t, filepath.Join(root, "src", "apps", "CMakeLists.txt"), "")
	assertFile(t, filepath.Join(root, "src", "libs", "CMakeLists.txt"), "")
	// The default build image is recorded and its Dockerfile generated.
	assertFile(t, filepath.Join(root, "cup.toml"), `base = "gcc:14"`)
	assertFile(t, filepath.Join(root, "docker", "proj", "Dockerfile"), "FROM gcc:14")

	// A second RunNew for the same name refuses to clobber the directory. The
	// build-tool + standard + compiler + base-image prompts run before the
	// existing-dir check.
	feed(t, "1\n1\n1\n1\ngcc\n1\n")
	if err := RunNew([]string{"proj"}); err == nil {
		t.Error("RunNew(existing) = nil error, want 'already exists'")
	}
}

// RunNew with the Make build tool scaffolds a discovery-based Makefile instead of
// CMakeLists.txt and gates the standard to the headers family.
func TestRunNewMakeEndToEnd(t *testing.T) {
	restore := scaffold.NewestCompilersFunc
	scaffold.NewestCompilersFunc = func() (int, int) { return 15, 20 }
	t.Cleanup(func() { scaffold.NewestCompilersFunc = restore })
	withStubTags(t, []string{"14", "13"}, nil)

	dir := t.TempDir()
	t.Chdir(dir)
	// build-tool Select: option 2 (make). standard Select: option 1 (C++17, newest
	// headers standard). "which compilers" option 1 (both); GCC floors for C++17
	// offer several, option 1; Clang option 1. Base image repo "gcc", tag option 1.
	feed(t, "2\n1\n1\n1\n1\ngcc\n1\n")
	if err := RunNew([]string{"mk"}); err != nil {
		t.Fatalf("RunNew(make): %v", err)
	}
	root := filepath.Join(dir, "mk")
	assertFile(t, filepath.Join(root, "cup.toml"), `build_tool = "make"`)
	assertFile(t, filepath.Join(root, "cup.toml"), "cpp_standard = 17")
	assertFile(t, filepath.Join(root, "Makefile"), "-std=c++17")
	assertFile(t, filepath.Join(root, ".gitignore"), "/build/")
	// No CMake anywhere in a Make project.
	if isFile(filepath.Join(root, "CMakeLists.txt")) {
		t.Error("Make project should not have a root CMakeLists.txt")
	}
	if isFile(filepath.Join(root, "src", "apps", "CMakeLists.txt")) {
		t.Error("Make project should not seed src/apps/CMakeLists.txt")
	}
	if !isDir(filepath.Join(root, "src", "libs")) {
		t.Error("Make project should create src/libs for discovery")
	}
}
