package cmd

import (
	"os"
	"path/filepath"
	"testing"
)

func TestHeaderExt(t *testing.T) {
	if headerExt(true) != ".h" {
		t.Errorf("headerExt(compiled) = %q, want .h", headerExt(true))
	}
	if headerExt(false) != ".hpp" {
		t.Errorf("headerExt(header-only) = %q, want .hpp", headerExt(false))
	}
}

func TestSourceTmpl(t *testing.T) {
	if sourceTmpl(true) != "source.h.tmpl" {
		t.Errorf("sourceTmpl(compiled) = %q", sourceTmpl(true))
	}
	if sourceTmpl(false) != "source.hpp.tmpl" {
		t.Errorf("sourceTmpl(header-only) = %q", sourceTmpl(false))
	}
}

// A C++17 project scaffolds a compiled component (class): a .h/.cpp pair plus a
// STATIC-library CMakeLists and a #include aggregator.
func TestCreateHeaderLibCompiled(t *testing.T) {
	proj := newProject(t, 17)
	feed(t, "math\n1\n\n") // lib name (Text), kind=class(1), symbol default (Math)
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib: %v", err)
	}
	libDir := filepath.Join(proj.Src(), "libs", "math")
	assertFile(t, filepath.Join(libDir, "Math.h"), "namespace")
	assertFile(t, filepath.Join(libDir, "Math.cpp"), "")
	assertFile(t, filepath.Join(libDir, "math.hpp"), "#include \"Math.h\"")
	assertFile(t, filepath.Join(libDir, "CMakeLists.txt"), "")
	assertFile(t, filepath.Join(proj.Src(), "libs", "CMakeLists.txt"), "add_subdirectory(math)")
}

// A header-only kind (templated-class, option 5) scaffolds a single .hpp.
func TestCreateHeaderLibHeaderOnly(t *testing.T) {
	proj := newProject(t, 17)
	feed(t, "meta\n5\n\n") // kind=templated-class(5), symbol default (Meta)
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib: %v", err)
	}
	libDir := filepath.Join(proj.Src(), "libs", "meta")
	assertFile(t, filepath.Join(libDir, "Meta.hpp"), "namespace")
	assertFile(t, filepath.Join(libDir, "meta.hpp"), "#include \"Meta.hpp\"")
	// No compiled definition for a header-only kind.
	if _, err := os.Stat(filepath.Join(libDir, "Meta.cpp")); err == nil {
		t.Error("header-only kind should not produce a .cpp")
	}
}

// Extending an existing header lib with a second compiled component promotes it
// and wires the new file into the aggregator and target.
func TestAddFileToHeaderLib(t *testing.T) {
	proj := newProject(t, 17)
	feed(t, "math\n1\n\n")
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib: %v", err)
	}
	libDir := filepath.Join(proj.Src(), "libs", "math")

	// what=file(1), filename, kind=class(1), symbol default.
	feed(t, "1\nvector\n1\n\n")
	if err := extendLib(proj, libDir); err != nil {
		t.Fatalf("extendLib: %v", err)
	}
	assertFile(t, filepath.Join(libDir, "vector.h"), "namespace")
	assertFile(t, filepath.Join(libDir, "vector.cpp"), "")
	assertFile(t, filepath.Join(libDir, "math.hpp"), "#include \"vector.h\"")
}
