package cmd

import (
	"path/filepath"
	"strings"
	"testing"
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
	std, err := chooseStandard()
	if err != nil || std < 11 {
		t.Fatalf("chooseStandard = %d, %v", std, err)
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
	dir := t.TempDir()
	t.Chdir(dir)
	// standard Select: option 1 (newest). name given as arg.
	feed(t, "1\n")
	if err := RunNew([]string{"proj"}); err != nil {
		t.Fatalf("RunNew: %v", err)
	}
	root := filepath.Join(dir, "proj")
	assertFile(t, filepath.Join(root, "cup.toml"), "proj")
	assertFile(t, filepath.Join(root, "CMakeLists.txt"), "proj")
	assertFile(t, filepath.Join(root, ".gitignore"), "")
	assertFile(t, filepath.Join(root, "src", "apps", "CMakeLists.txt"), "")
	assertFile(t, filepath.Join(root, "src", "libs", "CMakeLists.txt"), "")

	// A second RunNew for the same name refuses to clobber the directory.
	feed(t, "1\n")
	if err := RunNew([]string{"proj"}); err == nil {
		t.Error("RunNew(existing) = nil error, want 'already exists'")
	}
}
