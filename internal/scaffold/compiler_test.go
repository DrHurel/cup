package scaffold

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestMinCompilers(t *testing.T) {
	cases := map[int][2]int{
		23: {15, 17},
		20: {11, 16},
		17: {7, 5},
		14: {5, 4},
		11: {5, 4},
	}
	for std, want := range cases {
		gcc, clang := MinCompilers(std)
		if gcc != want[0] || clang != want[1] {
			t.Errorf("MinCompilers(%d) = (%d, %d), want (%d, %d)", std, gcc, clang, want[0], want[1])
		}
	}
}

func TestCompilerChoices(t *testing.T) {
	// C++23's baseline GCC (15) equals the fetched newest, so it is the sole option.
	gcc, clang := CompilerChoices(23, 15, 20)
	if len(gcc) != 1 || gcc[0] != 15 {
		t.Errorf("CompilerChoices(23) gcc = %v, want [15]", gcc)
	}
	// Clang ranges from its baseline up to the newest, oldest first.
	if len(clang) == 0 || clang[0] != 17 || clang[len(clang)-1] != 20 {
		t.Errorf("CompilerChoices(23) clang = %v, want 17..20", clang)
	}
	// A lower standard opens up more (older) GCC options, baseline first, and the
	// ceiling tracks whatever newest we discovered.
	gcc, _ = CompilerChoices(20, 16, 20)
	if gcc[0] != 11 || gcc[len(gcc)-1] != 16 {
		t.Errorf("CompilerChoices(20, newest 16) gcc = %v, want 11..16", gcc)
	}
	// A newest that lags the baseline (shouldn't happen, but be safe) collapses to
	// the baseline alone rather than yielding an empty list.
	gcc, _ = CompilerChoices(23, 14, 20)
	if len(gcc) != 1 || gcc[0] != 15 {
		t.Errorf("CompilerChoices(23, newest 14) gcc = %v, want [15]", gcc)
	}
}

func TestParseGCCNewest(t *testing.T) {
	index := `<a href="gcc-4.8.5/">gcc-4.8.5/</a>
<a href="gcc-14.2.0/">gcc-14.2.0/</a>
<a href="gcc-15.1.0/">gcc-15.1.0/</a>
<a href="summit/">summit/</a>`
	if got := parseGCCNewest([]byte(index)); got != 15 {
		t.Errorf("parseGCCNewest = %d, want 15", got)
	}
	if got := parseGCCNewest([]byte("no versions here")); got != 0 {
		t.Errorf("parseGCCNewest(none) = %d, want 0", got)
	}
}

func TestParseClangNewest(t *testing.T) {
	body := `[
		{"tag_name":"llvmorg-21.0.0-rc1","prerelease":true},
		{"tag_name":"llvmorg-20.1.8","prerelease":false},
		{"tag_name":"llvmorg-19.1.7","prerelease":false}
	]`
	if got := parseClangNewest([]byte(body)); got != 20 {
		t.Errorf("parseClangNewest = %d, want 20 (skipping the prerelease 21)", got)
	}
	if got := parseClangNewest([]byte("not json")); got != 0 {
		t.Errorf("parseClangNewest(bad) = %d, want 0", got)
	}
}

func TestCompilerGuard(t *testing.T) {
	guard := CompilerGuard(15, 17)
	if !strings.HasPrefix(guard, GuardStart) || !strings.HasSuffix(guard, GuardEnd) {
		t.Fatalf("guard not wrapped in markers:\n%s", guard)
	}
	for _, want := range []string{
		`CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15`,
		`CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17`,
		"requires GCC >= 15",
		"requires Clang >= 17",
	} {
		if !strings.Contains(guard, want) {
			t.Errorf("guard missing %q\n---\n%s", want, guard)
		}
	}
}

func TestCompilerGuardZeroDisables(t *testing.T) {
	// A zero version drops that compiler's branch; both zero drops the if entirely.
	only := CompilerGuard(15, 0)
	if strings.Contains(only, "Clang") {
		t.Errorf("clang=0 should not emit a Clang branch:\n%s", only)
	}
	if !strings.Contains(only, "GNU") {
		t.Errorf("gcc=15 should emit a GNU branch:\n%s", only)
	}
	none := CompilerGuard(0, 0)
	if strings.Contains(none, "if(") || strings.Contains(none, "FATAL_ERROR") {
		t.Errorf("both zero should emit no check:\n%s", none)
	}
	// Still delimited so `cup compiler` can rewrite it later.
	if !strings.HasPrefix(none, GuardStart) || !strings.HasSuffix(none, GuardEnd) {
		t.Errorf("empty guard must keep its markers:\n%s", none)
	}
}

func TestReplaceCompilerGuard(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "CMakeLists.txt")
	body := "project(demo)\n\n" + CompilerGuard(15, 17) + "\n\nset(CMAKE_CXX_STANDARD 23)\n"
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}

	if err := ReplaceCompilerGuard(root, path, 13, 0); err != nil {
		t.Fatalf("ReplaceCompilerGuard: %v", err)
	}
	got, _ := os.ReadFile(path)
	out := string(got)
	if !strings.Contains(out, "VERSION_LESS 13") {
		t.Errorf("guard not lowered to 13:\n%s", out)
	}
	if strings.Contains(out, "VERSION_LESS 17") || strings.Contains(out, "Clang") {
		t.Errorf("clang=0 should have dropped the Clang branch:\n%s", out)
	}
	// Surrounding lines are preserved.
	if !strings.Contains(out, "project(demo)") || !strings.Contains(out, "set(CMAKE_CXX_STANDARD 23)") {
		t.Errorf("surrounding CMake lines lost:\n%s", out)
	}
}

func TestReplaceCompilerGuardNoMarkers(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "CMakeLists.txt")
	if err := os.WriteFile(path, []byte("project(demo)\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ReplaceCompilerGuard(root, path, 15, 17); err == nil {
		t.Error("ReplaceCompilerGuard on a file without markers = nil error, want error")
	}
}
