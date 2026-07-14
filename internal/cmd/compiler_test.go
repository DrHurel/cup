package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"cup/internal/project"
	"cup/internal/scaffold"
)

func TestEffectiveCompilers(t *testing.T) {
	// No [compiler] table -> per-standard defaults.
	gcc, clang := effectiveCompilers(project.Config{CppStandard: 20})
	if gcc != 11 || clang != 16 {
		t.Errorf("effectiveCompilers(c++20, no floor) = (%d, %d), want (11, 16)", gcc, clang)
	}
	// An explicit floor wins, even a partial one (gcc pinned, clang unpinned).
	gcc, clang = effectiveCompilers(project.Config{
		CppStandard: 23,
		Compiler:    project.NewCompilerConfig(14, 0),
	})
	if gcc != 14 || clang != 0 {
		t.Errorf("effectiveCompilers(explicit gcc) = (%d, %d), want (14, 0)", gcc, clang)
	}
}

func TestParseCompilerFlags(t *testing.T) {
	image, noVerify, rest, err := parseCompilerFlags(
		[]string{"gcc", "15", "--image", "cxx:15", "--no-verify"})
	if err != nil {
		t.Fatalf("parseCompilerFlags: %v", err)
	}
	if image != "cxx:15" || !noVerify {
		t.Errorf("flags = (%q, %v), want (cxx:15, true)", image, noVerify)
	}
	if len(rest) != 2 || rest[0] != "gcc" || rest[1] != "15" {
		t.Errorf("positional rest = %v, want [gcc 15]", rest)
	}

	if _, _, _, err := parseCompilerFlags([]string{"--image"}); err == nil {
		t.Error("--image without a value = nil error, want error")
	}
}

// seedGuardedCMake overwrites the project's root CMakeLists with a guard block so
// setCompiler has markers to rewrite (newProject seeds a plain one).
func seedGuardedCMake(t *testing.T, proj *project.Project, gcc, clang int) {
	t.Helper()
	body := "project(demo VERSION 0.1.0 LANGUAGES C CXX)\n\n" +
		scaffold.CompilerGuard(gcc, clang) + "\n\nset(CMAKE_CXX_STANDARD 20)\n"
	if err := os.WriteFile(filepath.Join(proj.Root, cmakelists), []byte(body), 0o644); err != nil {
		t.Fatalf("seed guarded CMakeLists: %v", err)
	}
}

func TestSetCompilerNoVerify(t *testing.T) {
	proj := newProject(t, 20)
	seedGuardedCMake(t, proj, 11, 16)

	if err := setCompiler(proj, []string{"gcc", "12", "--no-verify"}); err != nil {
		t.Fatalf("setCompiler: %v", err)
	}

	// cup.toml now pins gcc=12 and keeps clang's materialised default (16).
	tomlPath := filepath.Join(proj.Root, project.Marker)
	assertFile(t, tomlPath, "gcc = 12")
	assertFile(t, tomlPath, "clang = 16")

	// The CMake guard tracks the new floor.
	cmakePath := filepath.Join(proj.Root, cmakelists)
	assertFile(t, cmakePath, "VERSION_LESS 12")
}

func TestSetCompilerRequiresImage(t *testing.T) {
	proj := newProject(t, 20)
	seedGuardedCMake(t, proj, 11, 16)

	// No verify_image, no --image, no --no-verify -> refuse before touching files.
	err := setCompiler(proj, []string{"gcc", "12"})
	if err == nil {
		t.Fatal("setCompiler without an image = nil error, want error")
	}
	// cup.toml must be untouched: the refused set wrote no gcc floor.
	b, _ := os.ReadFile(filepath.Join(proj.Root, project.Marker))
	if strings.Contains(string(b), "gcc = 12") {
		t.Errorf("cup.toml changed despite the refused set:\n%s", b)
	}
}

func TestSetCompilerBadArgs(t *testing.T) {
	proj := newProject(t, 20)
	seedGuardedCMake(t, proj, 11, 16)

	if err := setCompiler(proj, []string{"rustc", "1", "--no-verify"}); err == nil {
		t.Error("unknown compiler = nil error, want error")
	}
	if err := setCompiler(proj, []string{"gcc", "notanumber", "--no-verify"}); err == nil {
		t.Error("non-numeric version = nil error, want error")
	}
	if err := setCompiler(proj, []string{"gcc", "--no-verify"}); err == nil {
		t.Error("missing version = nil error, want error")
	}
}
