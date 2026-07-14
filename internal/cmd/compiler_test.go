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

func TestFloorLabel(t *testing.T) {
	if got := floorLabel(0); got != "(no floor)" {
		t.Errorf("floorLabel(0) = %q, want (no floor)", got)
	}
	if got := floorLabel(15); got != ">= 15" {
		t.Errorf("floorLabel(15) = %q, want >= 15", got)
	}
}

func TestShowCompilers(t *testing.T) {
	// No [compiler] table: shows per-standard defaults and the unset-image note.
	proj := newProject(t, 20)
	if err := showCompilers(proj); err != nil {
		t.Fatalf("showCompilers (no floor): %v", err)
	}
	// An explicit floor and a verify image exercise the other branches.
	proj.Config.Compiler = project.NewCompilerConfig(14, 0)
	proj.Config.Compiler.VerifyImage = "cxx:14"
	if err := showCompilers(proj); err != nil {
		t.Fatalf("showCompilers (with floor): %v", err)
	}
}

func TestVerifyCompilerNoImage(t *testing.T) {
	proj := newProject(t, 20)
	// No verify_image and no --image: refuse before reaching docker.
	if err := verifyCompiler(proj, nil); err == nil {
		t.Error("verifyCompiler without an image = nil error, want error")
	}
	// A stray positional argument is rejected too.
	if err := verifyCompiler(proj, []string{"extra"}); err == nil {
		t.Error("verifyCompiler with a positional arg = nil error, want error")
	}
}

func TestRunCompilerDispatch(t *testing.T) {
	proj := newProject(t, 20)
	t.Chdir(proj.Root) // RunCompiler resolves the project from the working dir

	// No args -> show; this project pins no floor, so it just prints defaults.
	if err := RunCompiler(nil); err != nil {
		t.Fatalf("RunCompiler(nil): %v", err)
	}
	if err := RunCompiler([]string{"show"}); err != nil {
		t.Fatalf("RunCompiler(show): %v", err)
	}
	if err := RunCompiler([]string{"bogus"}); err == nil {
		t.Error("RunCompiler(bogus) = nil error, want error")
	}
}

// resolveVerifyImage prefers an explicit override, then a legacy verify_image, and
// errors when the project offers no image at all — none of which touch docker.
func TestResolveVerifyImage(t *testing.T) {
	proj := newProject(t, 20)

	// Explicit override wins and is returned as-is.
	if got, err := resolveVerifyImage(proj, "cxx:15"); err != nil || got != "cxx:15" {
		t.Fatalf("resolveVerifyImage(override) = %q, %v; want cxx:15, nil", got, err)
	}

	// No override, no default image, no verify_image -> error.
	if _, err := resolveVerifyImage(proj, ""); err == nil {
		t.Error("resolveVerifyImage(no target) = nil error, want error")
	}

	// A legacy verify_image is used directly.
	proj.Config.Compiler.VerifyImage = "cup-cxx:latest"
	if got, err := resolveVerifyImage(proj, ""); err != nil || got != "cup-cxx:latest" {
		t.Fatalf("resolveVerifyImage(verify_image) = %q, %v; want cup-cxx:latest, nil", got, err)
	}
}

func TestHasVerifyTarget(t *testing.T) {
	proj := newProject(t, 20)
	if hasVerifyTarget(proj, "") {
		t.Error("hasVerifyTarget(no target) = true, want false")
	}
	if !hasVerifyTarget(proj, "cxx:15") {
		t.Error("hasVerifyTarget(override) = false, want true")
	}
	proj.Config.Docker.Images = []project.DockerImage{{Name: "demo", Base: "gcc:14", Default: true}}
	if !hasVerifyTarget(proj, "") {
		t.Error("hasVerifyTarget(default image) = false, want true")
	}
}

func TestCommitCompilerFloorRestoresOnGuardFailure(t *testing.T) {
	// newProject seeds a root CMakeLists with no guard markers, so applyCompilerFloor
	// fails midway; commitCompilerFloor must roll cup.toml back byte-for-byte.
	proj := newProject(t, 20)
	tomlPath := filepath.Join(proj.Root, project.Marker)
	before, _ := os.ReadFile(tomlPath)

	err := setCompiler(proj, []string{"gcc", "12", "--no-verify"})
	if err == nil {
		t.Fatal("setCompiler onto a marker-less CMakeLists = nil error, want error")
	}
	after, _ := os.ReadFile(tomlPath)
	if string(before) != string(after) {
		t.Errorf("cup.toml not restored after a failed set:\nbefore:\n%s\nafter:\n%s", before, after)
	}
}
