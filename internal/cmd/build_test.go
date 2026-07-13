package cmd

import (
	"os"
	"path/filepath"
	"testing"
)

func TestParseMode(t *testing.T) {
	cases := []struct {
		args     []string
		wantMode string
		wantRest []string
	}{
		{nil, "Debug", nil},
		{[]string{"Release"}, "Release", []string{}},
		{[]string{"Coverage", "app"}, "Coverage", []string{"app"}},
		{[]string{"app"}, "Debug", []string{"app"}}, // non-mode first arg stays
	}
	for _, c := range cases {
		mode, rest := parseMode(c.args)
		if mode != c.wantMode {
			t.Errorf("parseMode(%v) mode = %q, want %q", c.args, mode, c.wantMode)
		}
		if len(rest) != len(c.wantRest) {
			t.Errorf("parseMode(%v) rest = %v, want %v", c.args, rest, c.wantRest)
		}
	}
}

func TestBuildDir(t *testing.T) {
	proj := newProject(t, 23)
	got := buildDir(proj, "Release")
	if got != filepath.Join(proj.Root, "build", "Release") {
		t.Errorf("buildDir = %q", got)
	}
}

func TestResolveApp(t *testing.T) {
	proj := newProject(t, 23)

	// No apps -> error.
	if _, _, err := resolveApp(proj, nil); err == nil {
		t.Error("resolveApp(no apps) = nil error, want error")
	}

	// One app, no explicit name -> that app, remaining args after "--".
	feed(t, "greeter\n\n")
	if err := addApp(proj); err != nil {
		t.Fatalf("addApp: %v", err)
	}
	name, args, err := resolveApp(proj, []string{"--", "-v"})
	if err != nil || name != "greeter" {
		t.Fatalf("resolveApp(one) = %q, %v", name, err)
	}
	if len(args) != 1 || args[0] != "-v" {
		t.Errorf("resolveApp program args = %v, want [-v]", args)
	}

	// Explicit app name is taken from the first token.
	name, _, err = resolveApp(proj, []string{"greeter"})
	if err != nil || name != "greeter" {
		t.Fatalf("resolveApp(named) = %q, %v", name, err)
	}

	// Two apps, no name -> prompt (Select option 2).
	feed(t, "other\n\n")
	if err := addApp(proj); err != nil {
		t.Fatalf("addApp second: %v", err)
	}
	feed(t, "2\n")
	name, _, err = resolveApp(proj, nil)
	if err != nil || name == "" {
		t.Fatalf("resolveApp(prompt) = %q, %v", name, err)
	}
}

func TestRunClean(t *testing.T) {
	proj := newProject(t, 23)
	buildPath := proj.Path("build", "Debug")
	if err := os.MkdirAll(buildPath, 0o755); err != nil {
		t.Fatal(err)
	}
	// RunClean uses project.Find via cwd; chdir into the project.
	t.Chdir(proj.Root)
	if err := RunClean(nil); err != nil {
		t.Fatalf("RunClean: %v", err)
	}
	if _, err := os.Stat(proj.Path("build")); !os.IsNotExist(err) {
		t.Errorf("build/ still present after clean: %v", err)
	}
}

func TestRunConfigureOutsideProject(t *testing.T) {
	// A directory with no cup.toml makes project.Find fail.
	t.Chdir(t.TempDir())
	if err := RunConfigure(nil); err == nil {
		t.Error("RunConfigure outside project = nil error, want error")
	}
}
