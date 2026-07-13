package tmpl

import (
	"os"
	"path/filepath"
	"testing"
)

func contains(list []string, want string) bool {
	for _, s := range list {
		if s == want {
			return true
		}
	}
	return false
}

func TestExistsBuiltin(t *testing.T) {
	if !Exists("", "headers", "class", "source.h.tmpl") {
		t.Error("Exists reported built-in headers/class/source.h.tmpl missing")
	}
	if Exists("", "headers", "class", "nope.tmpl") {
		t.Error("Exists reported a nonexistent template present")
	}
}

func TestReadBuiltin(t *testing.T) {
	b, err := Read("", "headers", "class", "source.h.tmpl")
	if err != nil {
		t.Fatalf("Read built-in returned error: %v", err)
	}
	if len(b) == 0 {
		t.Error("Read returned empty content for a built-in template")
	}
}

func TestIsCompiled(t *testing.T) {
	// class ships a .h/.cpp pair -> compiled.
	if !IsCompiled("", "headers", "class") {
		t.Error("headers/class should be compiled (has source.h + source.cpp)")
	}
	// templated-class is header-only (.hpp) -> not compiled.
	if IsCompiled("", "headers", "templated-class") {
		t.Error("headers/templated-class should not be compiled (header-only)")
	}
	// modules kinds are never compiled in the headers sense.
	if IsCompiled("", "modules", "class") {
		t.Error("modules/class should not report as compiled")
	}
}

func TestKindsExcludesSpecialDirs(t *testing.T) {
	kinds := Kinds("", "headers")
	for _, want := range []string{"class", "interface", "templated-class"} {
		if !contains(kinds, want) {
			t.Errorf("Kinds(headers) missing component kind %q; got %v", want, kinds)
		}
	}
	for _, excluded := range []string{"app", "test", "project"} {
		if contains(kinds, excluded) {
			t.Errorf("Kinds(headers) should exclude special dir %q; got %v", excluded, kinds)
		}
	}
}

func TestKindsModules(t *testing.T) {
	if kinds := Kinds("", "modules"); !contains(kinds, "class") {
		t.Errorf("Kinds(modules) missing class (has source.cppm.tmpl); got %v", kinds)
	}
}

func TestBuiltinKindsIncludesSpecialDirs(t *testing.T) {
	kinds := BuiltinKinds("headers")
	for _, want := range []string{"app", "test", "project", "class"} {
		if !contains(kinds, want) {
			t.Errorf("BuiltinKinds(headers) missing %q; got %v", want, kinds)
		}
	}
}

func TestProjectOverrideWins(t *testing.T) {
	root := t.TempDir()
	local := filepath.Join(root, ProjectTemplateDir, "class", "source.h.tmpl")
	if err := os.MkdirAll(filepath.Dir(local), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(local, []byte("OVERRIDE"), 0o644); err != nil {
		t.Fatal(err)
	}

	b, err := Read(root, "headers", "class", "source.h.tmpl")
	if err != nil {
		t.Fatalf("Read with override returned error: %v", err)
	}
	if string(b) != "OVERRIDE" {
		t.Errorf("Read = %q, want project override %q", b, "OVERRIDE")
	}
	if !Exists(root, "headers", "class", "source.h.tmpl") {
		t.Error("Exists should see the project override")
	}
}

func TestProjectOverrideAddsNewKind(t *testing.T) {
	root := t.TempDir()
	// A brand-new headers component kind, defined only in the project.
	local := filepath.Join(root, ProjectTemplateDir, "widget", "source.hpp.tmpl")
	if err := os.MkdirAll(filepath.Dir(local), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(local, []byte("// widget"), 0o644); err != nil {
		t.Fatal(err)
	}

	if kinds := Kinds(root, "headers"); !contains(kinds, "widget") {
		t.Errorf("Kinds should include project-local kind 'widget'; got %v", kinds)
	}
}

func TestCopyBuiltin(t *testing.T) {
	dst := filepath.Join(t.TempDir(), "copied")
	if err := CopyBuiltin("headers", "class", dst); err != nil {
		t.Fatalf("CopyBuiltin returned error: %v", err)
	}
	for _, name := range []string{"source.h.tmpl", "source.cpp.tmpl", "CMakeLists.txt.tmpl"} {
		if _, err := os.Stat(filepath.Join(dst, name)); err != nil {
			t.Errorf("CopyBuiltin did not write %s: %v", name, err)
		}
	}
}
