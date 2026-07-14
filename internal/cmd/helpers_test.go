package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"cup/internal/project"
	"cup/internal/ui"
)

// feed scripts the interactive prompts for the duration of the test. Each
// ui.Text / ui.Confirm consumes one line; a ui.Select (which, off a terminal,
// falls back to a numbered menu) consumes one line holding the 1-based choice.
func feed(t *testing.T, input string) {
	t.Helper()
	restore := ui.SetInput(strings.NewReader(input))
	t.Cleanup(restore)
}

// newProject writes a minimal cup project skeleton under a fresh temp dir and
// returns it. std selects the family: >= 20 scaffolds modules, below that headers.
func newProject(t *testing.T, std int) *project.Project {
	t.Helper()
	root := t.TempDir()
	if err := project.WriteConfig(root, project.Config{Name: "demo", CppStandard: std}); err != nil {
		t.Fatalf("WriteConfig: %v", err)
	}
	// The add flows register targets into these parents; seed them empty like
	// `cup new` does so EnsureLine has a file to append to.
	for _, sub := range []string{"apps", "libs"} {
		dir := filepath.Join(root, "src", sub)
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatalf("mkdir %s: %v", dir, err)
		}
		if err := os.WriteFile(filepath.Join(dir, "CMakeLists.txt"), nil, 0o644); err != nil {
			t.Fatalf("seed CMakeLists: %v", err)
		}
	}
	if err := os.WriteFile(filepath.Join(root, "CMakeLists.txt"), []byte("# root\n"), 0o644); err != nil {
		t.Fatalf("seed root CMakeLists: %v", err)
	}
	return &project.Project{Root: root, Config: project.Config{Name: "demo", CppStandard: std}}
}

// newProjectWithImage is newProject plus a default build image named "demo" on a
// gcc:14 base, for exercising the flows that keep docker/<name>/Dockerfile in sync
// with the project's dependencies.
func newProjectWithImage(t *testing.T, std int) *project.Project {
	t.Helper()
	proj := newProject(t, std)
	proj.Config.Docker = project.DockerConfig{Images: []project.DockerImage{
		{Name: "demo", Base: "gcc:14", Default: true},
	}}
	return proj
}

// stubRunCommand replaces runCommand for the duration of a test, recording each
// invocation as "name arg1 arg2 …" so tests can assert on what would have been
// shelled out without running git/docker/apt. It returns a pointer to the growing
// log; failWhen (optional) makes a matching call fail.
func stubRunCommand(t *testing.T, failWhen func(name string, args []string) error) *[]string {
	t.Helper()
	prev := runCommand
	var calls []string
	runCommand = func(dir, name string, args ...string) error {
		calls = append(calls, strings.TrimSpace(name+" "+strings.Join(args, " ")))
		if failWhen != nil {
			return failWhen(name, args)
		}
		return nil
	}
	t.Cleanup(func() { runCommand = prev })
	return &calls
}

// assertFile fails unless path exists and (when substr is non-empty) contains it.
func assertFile(t *testing.T, path, substr string) {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("expected file %s: %v", path, err)
	}
	if substr != "" && !strings.Contains(string(b), substr) {
		t.Errorf("file %s does not contain %q\n---\n%s", path, substr, b)
	}
}
