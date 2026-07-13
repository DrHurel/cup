package scaffold

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"cup/internal/tmpl"
)

// writeTemplate drops a project-local override template so Render/tmpl.Read read
// deterministic content instead of a built-in.
func writeTemplate(t *testing.T, root, kind, name, content string) {
	t.Helper()
	dir := filepath.Join(root, tmpl.ProjectTemplateDir, kind, name)
	if err := os.MkdirAll(filepath.Dir(dir), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(dir, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestRenderSubstitutes(t *testing.T) {
	root := t.TempDir()
	writeTemplate(t, root, "class", "source.h.tmpl", "class {{Name}} in {{ns}} {};")

	got, err := Render(root, "headers", "class", "source.h.tmpl", map[string]string{
		"Name": "Widget",
		"ns":   "ui",
	})
	if err != nil {
		t.Fatalf("Render returned error: %v", err)
	}
	if want := "class Widget in ui {};"; got != want {
		t.Errorf("Render = %q, want %q", got, want)
	}
}

// A variable whose value contains another placeholder must be resolved to a
// fixed point.
func TestRenderResolvesNestedPlaceholders(t *testing.T) {
	root := t.TempDir()
	writeTemplate(t, root, "greet", "hello.tmpl", "{{greeting}}")

	got, err := Render(root, "headers", "greet", "hello.tmpl", map[string]string{
		"greeting": "hi {{name}}",
		"name":     "cup",
	})
	if err != nil {
		t.Fatalf("Render returned error: %v", err)
	}
	if want := "hi cup"; got != want {
		t.Errorf("Render = %q, want %q", got, want)
	}
}

func TestRenderUnresolvedPlaceholder(t *testing.T) {
	root := t.TempDir()
	writeTemplate(t, root, "class", "source.h.tmpl", "{{Name}} {{Missing}} {{Missing}}")

	_, err := Render(root, "headers", "class", "source.h.tmpl", map[string]string{"Name": "X"})
	if err == nil {
		t.Fatal("Render = nil error, want unresolved-placeholder error")
	}
	if !strings.Contains(err.Error(), "{{Missing}}") {
		t.Errorf("error %q should name the unresolved placeholder", err)
	}
	// The unresolved set is de-duplicated, so {{Missing}} appears once.
	if strings.Count(err.Error(), "{{Missing}}") != 1 {
		t.Errorf("error %q should list {{Missing}} exactly once", err)
	}
}

func TestRenderMissingTemplate(t *testing.T) {
	root := t.TempDir()
	if _, err := Render(root, "headers", "nope", "source.h.tmpl", nil); err == nil {
		t.Fatal("Render for missing template = nil error, want error")
	}
}

func TestEnsureFile(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "sub", "dir", "file.txt")

	if err := EnsureFile(root, path, "first"); err != nil {
		t.Fatalf("EnsureFile (create) returned error: %v", err)
	}
	if b, _ := os.ReadFile(path); string(b) != "first" {
		t.Errorf("file content = %q, want %q", b, "first")
	}

	// A second call must not overwrite existing content.
	if err := EnsureFile(root, path, "second"); err != nil {
		t.Fatalf("EnsureFile (existing) returned error: %v", err)
	}
	if b, _ := os.ReadFile(path); string(b) != "first" {
		t.Errorf("EnsureFile overwrote existing file: content = %q, want %q", b, "first")
	}
}

func TestWriteFileCreatesNested(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "a", "b", "c.txt")

	// A fresh path never prompts, so this exercises the non-interactive path.
	wrote, err := WriteFile(root, path, "content")
	if err != nil {
		t.Fatalf("WriteFile returned error: %v", err)
	}
	if !wrote {
		t.Error("WriteFile reported not written for a new file")
	}
	if b, _ := os.ReadFile(path); string(b) != "content" {
		t.Errorf("file content = %q, want %q", b, "content")
	}
}
