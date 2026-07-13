package cmd

import (
	"os"
	"path/filepath"
	"testing"
)

// breakBuild replaces the root CMakeLists with invalid CMake so `cmake` fails
// fast and deterministically, regardless of the installed cmake version.
func breakBuild(t *testing.T, root string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(root, "CMakeLists.txt"), []byte("this is not valid cmake ((("), 0o644); err != nil {
		t.Fatal(err)
	}
}

// The build wrappers shell out to cmake/ctest. Against a deliberately broken
// project cmake fails fast, so these exercise the dispatch + project.Find +
// parseMode paths and surface the tool error.
func TestBuildWrappersSurfaceCmakeError(t *testing.T) {
	proj := newProject(t, 23)
	breakBuild(t, proj.Root)
	t.Chdir(proj.Root)

	for name, run := range map[string]func([]string) error{
		"RunConfigure": RunConfigure,
		"RunBuild":     RunBuild,
		"RunTest":      RunTest,
		"RunRebuild":   RunRebuild,
		"RunRetest":    RunRetest,
	} {
		if err := run(nil); err == nil {
			t.Errorf("%s on stub project = nil error, want cmake failure", name)
		}
	}
}

func TestRunRunResolvesThenFails(t *testing.T) {
	proj := newProject(t, 23)
	// Give it one app so resolveApp succeeds; the build then fails on the stub.
	feed(t, "greeter\n\n")
	if err := addApp(proj); err != nil {
		t.Fatalf("addApp: %v", err)
	}
	breakBuild(t, proj.Root)
	t.Chdir(proj.Root)
	if err := RunRun([]string{"greeter"}); err == nil {
		t.Error("RunRun on stub project = nil error, want build failure")
	}
}

func TestBuildWrappersOutsideProject(t *testing.T) {
	t.Chdir(t.TempDir())
	for name, run := range map[string]func([]string) error{
		"RunBuild": RunBuild,
		"RunTest":  RunTest,
		"RunRun":   RunRun,
	} {
		if err := run(nil); err == nil {
			t.Errorf("%s outside project = nil error, want error", name)
		}
	}
}

// RunAdd with a category argument dispatches straight to that scaffolder.
func TestRunAddWithArg(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	feed(t, "smoke\n") // test name; no libs, so no module prompt
	if err := RunAdd([]string{"test"}); err != nil {
		t.Fatalf("RunAdd(test): %v", err)
	}
	assertFile(t, filepath.Join(proj.Src(), "tests", "smoke.cpp"), "")
}

// RunAdd with no argument prompts for a category, scaffolds, then asks to add
// another; declining ends the loop.
func TestRunAddInteractiveLoop(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	// category=test(3), test name, add another? -> n
	feed(t, "3\nsmoke\nn\n")
	if err := RunAdd(nil); err != nil {
		t.Fatalf("RunAdd(nil): %v", err)
	}
	assertFile(t, filepath.Join(proj.Src(), "tests", "smoke.cpp"), "")
}

func TestRunAddOutsideProject(t *testing.T) {
	t.Chdir(t.TempDir())
	if err := RunAdd([]string{"app"}); err == nil {
		t.Error("RunAdd outside project = nil error, want error")
	}
}
