package cmd

import (
	"path/filepath"
	"testing"

	"cup/internal/tmpl"
)

func TestRunTemplateList(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	if err := RunTemplate([]string{"list"}); err != nil {
		t.Fatalf("RunTemplate list: %v", err)
	}
	// Default sub is list.
	if err := RunTemplate(nil); err != nil {
		t.Fatalf("RunTemplate (default): %v", err)
	}
}

func TestRunTemplateUnknown(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	if err := RunTemplate([]string{"bogus"}); err == nil {
		t.Error("RunTemplate(bogus) = nil error, want error")
	}
}

func TestTemplateNewCopiesBuiltin(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	// base Select over BuiltinKinds (app, class, ...): class is option 2. Then a
	// name for the Text prompt.
	feed(t, "2\nmyclass\n")
	if err := RunTemplate([]string{"new"}); err != nil {
		t.Fatalf("RunTemplate new: %v", err)
	}
	dst := proj.Path(tmpl.ProjectTemplateDir, "myclass")
	assertFile(t, filepath.Join(dst, "source.cppm.tmpl"), "")
}

func TestTemplateNewDeclineOverwrite(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)

	feed(t, "1\nmyclass\n")
	if err := RunTemplate([]string{"new"}); err != nil {
		t.Fatalf("RunTemplate new: %v", err)
	}

	// Second attempt at the same name: base(2), name(myclass), then decline the
	// overwrite confirm.
	feed(t, "2\nmyclass\nn\n")
	if err := RunTemplate([]string{"new"}); err != nil {
		t.Fatalf("RunTemplate new (decline): %v", err)
	}
}
