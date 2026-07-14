package project

import (
	"os"
	"path/filepath"
	"testing"
)

func TestConfigStandard(t *testing.T) {
	if got := (Config{}).Standard(); got != 23 {
		t.Errorf("unset Standard() = %d, want 23 (default)", got)
	}
	if got := (Config{CppStandard: 17}).Standard(); got != 17 {
		t.Errorf("Standard() = %d, want 17", got)
	}
}

func TestCompilerHasFloor(t *testing.T) {
	if (CompilerConfig{}).HasFloor() {
		t.Error("empty CompilerConfig reports a floor, want none")
	}
	if !NewCompilerConfig(15, 0).HasFloor() {
		t.Error("gcc-only CompilerConfig reports no floor, want one")
	}
	if !NewCompilerConfig(0, 17).HasFloor() {
		t.Error("clang-only CompilerConfig reports no floor, want one")
	}
	// verify_image alone is not a version floor.
	if (CompilerConfig{VerifyImage: "cxx:15"}).HasFloor() {
		t.Error("verify_image alone reports a floor, want none")
	}
}

func TestNewCompilerConfig(t *testing.T) {
	// 0 leaves a compiler unpinned (nil); a positive version pins it.
	cc := NewCompilerConfig(15, 0)
	if cc.GCCFloor() != 15 {
		t.Errorf("GCCFloor() = %d, want 15", cc.GCCFloor())
	}
	if cc.Clang != nil || cc.ClangFloor() != 0 {
		t.Errorf("clang should be unpinned, got %v (floor %d)", cc.Clang, cc.ClangFloor())
	}
}

func TestUsesModules(t *testing.T) {
	cases := map[int]bool{
		0:  true, // default 23
		23: true,
		20: true,
		17: false,
		11: false,
	}
	for std, want := range cases {
		p := &Project{Config: Config{CppStandard: std}}
		if got := p.UsesModules(); got != want {
			t.Errorf("UsesModules() with cpp_standard=%d = %v, want %v", std, got, want)
		}
	}
}

func TestSrcAndPath(t *testing.T) {
	p := &Project{Root: filepath.FromSlash("/proj")}
	if got, want := p.Src(), filepath.FromSlash("/proj/src"); got != want {
		t.Errorf("Src() = %q, want %q", got, want)
	}
	if got, want := p.Path("a", "b"), filepath.FromSlash("/proj/a/b"); got != want {
		t.Errorf("Path() = %q, want %q", got, want)
	}
}

func TestWriteConfigRoundTrip(t *testing.T) {
	root := canonicalTempDir(t)
	cfg := Config{Name: "demo", CupVersion: "0.1.0", CppStandard: 20}
	if err := WriteConfig(root, cfg); err != nil {
		t.Fatalf("WriteConfig returned error: %v", err)
	}
	if _, err := os.Stat(filepath.Join(root, Marker)); err != nil {
		t.Fatalf("marker %s not written: %v", Marker, err)
	}

	p := findFrom(t, root)
	if p.Config != cfg {
		t.Errorf("round-tripped config = %+v, want %+v", p.Config, cfg)
	}
	if p.Root != root {
		t.Errorf("Find root = %q, want %q", p.Root, root)
	}
}

func TestFindWalksUp(t *testing.T) {
	root := canonicalTempDir(t)
	if err := WriteConfig(root, Config{Name: "demo"}); err != nil {
		t.Fatal(err)
	}
	nested := filepath.Join(root, "src", "libs", "utils")
	if err := os.MkdirAll(nested, 0o755); err != nil {
		t.Fatal(err)
	}

	p := findFrom(t, nested)
	if p.Root != root {
		t.Errorf("Find from nested dir = %q, want project root %q", p.Root, root)
	}
}

func TestFindNoProject(t *testing.T) {
	dir := t.TempDir()
	restore := chdir(t, dir)
	defer restore()

	if _, err := Find(); err == nil {
		t.Fatal("Find outside any project = nil error, want error")
	}
}

func TestFindInvalidToml(t *testing.T) {
	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, Marker), []byte("name = \"unterminated"), 0o644); err != nil {
		t.Fatal(err)
	}
	restore := chdir(t, root)
	defer restore()

	if _, err := Find(); err == nil {
		t.Fatal("Find with malformed cup.toml = nil error, want error")
	}
}

// findFrom runs Find with the working directory set to dir, restoring it after.
func findFrom(t *testing.T, dir string) *Project {
	t.Helper()
	restore := chdir(t, dir)
	defer restore()
	p, err := Find()
	if err != nil {
		t.Fatalf("Find returned error: %v", err)
	}
	return p
}

// canonicalTempDir returns a temp dir with symlinks resolved, so it matches the
// path Find derives from os.Getwd() (e.g. /tmp -> /private/tmp on macOS).
func canonicalTempDir(t *testing.T) string {
	t.Helper()
	dir, err := filepath.EvalSymlinks(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	return dir
}

func chdir(t *testing.T, dir string) func() {
	t.Helper()
	prev, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatal(err)
	}
	return func() { _ = os.Chdir(prev) }
}
