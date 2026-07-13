package cmd

import (
	"path/filepath"
	"testing"

	"cup/internal/project"
)

func TestFamily(t *testing.T) {
	if got := family(&project.Project{Config: project.Config{CppStandard: 23}}); got != "modules" {
		t.Errorf("family(c++23) = %q, want modules", got)
	}
	if got := family(&project.Project{Config: project.Config{CppStandard: 17}}); got != "headers" {
		t.Errorf("family(c++17) = %q, want headers", got)
	}
}

func TestStdVars(t *testing.T) {
	proj := &project.Project{Config: project.Config{CppStandard: 23}}
	vars := stdVars(proj, "name", "widget", "extra", "value")
	if vars["name"] != "widget" || vars["extra"] != "value" {
		t.Errorf("stdVars did not merge overrides: %v", vars)
	}
	if _, ok := vars["std_number"]; !ok {
		t.Errorf("stdVars missing per-standard keys: %v", vars)
	}
	// An odd trailing key without a value is ignored rather than panicking.
	if got := stdVars(proj, "dangling"); got["dangling"] != "" {
		t.Errorf("dangling key should be ignored, got %v", got)
	}
}

func TestRelTo(t *testing.T) {
	if got := relTo("/a/b", "/a/b/c/d"); got != filepath.Join("c", "d") {
		t.Errorf("relTo = %q, want c/d", got)
	}
	// An unrelatable path (different volume semantics) falls back to the input.
	if got := relTo("/a/b", "/a/b"); got != "." {
		t.Errorf("relTo(same) = %q, want .", got)
	}
}

func TestTestModuleImport(t *testing.T) {
	mod := &project.Project{Config: project.Config{CppStandard: 23}}
	if got := testModuleImport(mod, ""); got != "" {
		t.Errorf("empty module = %q, want empty", got)
	}
	if got := testModuleImport(mod, "math"); got != "import math;\n" {
		t.Errorf("module import = %q", got)
	}
	hdr := &project.Project{Config: project.Config{CppStandard: 17}}
	if got := testModuleImport(hdr, "math"); got != "#include \"math.hpp\"\n" {
		t.Errorf("header import = %q", got)
	}
}

func TestPickOrNew(t *testing.T) {
	// No options: prompt straight for a new name.
	feed(t, "fresh\n")
	got, err := pickOrNew("pick?", nil, "new?", nil)
	if err != nil || got != "fresh" {
		t.Fatalf("pickOrNew(empty) = %q, %v", got, err)
	}

	// Selecting an existing option returns it.
	feed(t, "1\n")
	got, err = pickOrNew("pick?", []string{"alpha", "beta"}, "new?", nil)
	if err != nil || got != "alpha" {
		t.Fatalf("pickOrNew(existing) = %q, %v", got, err)
	}

	// Selecting the [new…] sentinel (last entry) prompts for a name.
	feed(t, "3\nzeta\n")
	got, err = pickOrNew("pick?", []string{"alpha", "beta"}, "new?", nil)
	if err != nil || got != "zeta" {
		t.Fatalf("pickOrNew(new) = %q, %v", got, err)
	}
}

func TestChooseKind(t *testing.T) {
	// "class" is offered and chosen (option 1 in the sorted list).
	feed(t, "1\n")
	got, err := chooseKind("", "modules")
	if err != nil || got != "class" {
		t.Fatalf("chooseKind = %q, %v", got, err)
	}
	// An unknown family has no kinds and errors.
	if _, err := chooseKind("", "nope"); err == nil {
		t.Error("chooseKind(unknown family) = nil error, want error")
	}
}

func TestAddApp(t *testing.T) {
	proj := newProject(t, 23)
	feed(t, "greeter\n\n") // name, default filename (greeter.cpp)
	if err := addApp(proj); err != nil {
		t.Fatalf("addApp: %v", err)
	}
	appDir := filepath.Join(proj.Src(), "apps", "greeter")
	assertFile(t, filepath.Join(appDir, "greeter.cpp"), "")
	assertFile(t, filepath.Join(appDir, "CMakeLists.txt"), "greeter")
	assertFile(t, filepath.Join(proj.Src(), "apps", "CMakeLists.txt"), "add_subdirectory(greeter)")
}

func TestAddLibModule(t *testing.T) {
	proj := newProject(t, 23)
	// pickOrNew (no existing libs -> Text name), chooseKind (class), symbol default.
	feed(t, "math\n1\n\n")
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib: %v", err)
	}
	libDir := filepath.Join(proj.Src(), "libs", "math")
	assertFile(t, filepath.Join(libDir, "Math.cppm"), "namespace")
	assertFile(t, filepath.Join(libDir, "math.cppm"), "export module")
	assertFile(t, filepath.Join(libDir, "CMakeLists.txt"), "")
	assertFile(t, filepath.Join(proj.Src(), "libs", "CMakeLists.txt"), "add_subdirectory(math)")
}

func TestExtendLibAddFileAndSubfolder(t *testing.T) {
	proj := newProject(t, 23)
	libDir := filepath.Join(proj.Src(), "libs", "math")

	// Create the lib first.
	feed(t, "math\n1\n\n")
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib: %v", err)
	}

	// Extend it with a new file: what=file(1), filename, kind(1), symbol default.
	feed(t, "1\nvector\n1\n\n")
	if err := extendLib(proj, libDir); err != nil {
		t.Fatalf("extendLib file: %v", err)
	}
	assertFile(t, filepath.Join(libDir, "vector.cppm"), "namespace")

	// Extend it with a subfolder: what=subfolder(2), new name (Text), then the
	// nested createLibAt asks kind(1) + symbol default.
	feed(t, "2\ndetail\n1\n\n")
	if err := extendLib(proj, libDir); err != nil {
		t.Fatalf("extendLib subfolder: %v", err)
	}
	subDir := filepath.Join(libDir, "detail")
	assertFile(t, filepath.Join(subDir, "detail.cppm"), "export module")
	assertFile(t, filepath.Join(libDir, "CMakeLists.txt"), "add_subdirectory(detail)")
}

func TestChooseTestModule(t *testing.T) {
	proj := newProject(t, 23)
	// No libs yet -> returns "" with no prompt.
	got, err := chooseTestModule(proj)
	if err != nil || got != "" {
		t.Fatalf("chooseTestModule(no libs) = %q, %v", got, err)
	}

	// Add a lib, then pick it (option 2, after the [none] sentinel).
	feed(t, "math\n1\n\n")
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib: %v", err)
	}
	feed(t, "2\n")
	got, err = chooseTestModule(proj)
	if err != nil || got != "math" {
		t.Fatalf("chooseTestModule = %q, %v", got, err)
	}

	// Picking [none] (option 1) yields "".
	feed(t, "1\n")
	got, err = chooseTestModule(proj)
	if err != nil || got != "" {
		t.Fatalf("chooseTestModule(none) = %q, %v", got, err)
	}
}

func TestAddTest(t *testing.T) {
	proj := newProject(t, 23)
	// No libs -> chooseTestModule takes no input; just the test name.
	feed(t, "smoke\n")
	if err := addTest(proj); err != nil {
		t.Fatalf("addTest: %v", err)
	}
	testsDir := filepath.Join(proj.Src(), "tests")
	assertFile(t, filepath.Join(testsDir, "smoke.cpp"), "")
	assertFile(t, filepath.Join(testsDir, "CMakeLists.txt"), "add_executable(smoke smoke.cpp)")
	assertFile(t, filepath.Join(proj.Root, "CMakeLists.txt"), "add_subdirectory(src/tests)")
}

func TestDispatchCategoryUnknown(t *testing.T) {
	proj := newProject(t, 23)
	if err := dispatchCategory(proj, "bogus"); err == nil {
		t.Error("dispatchCategory(bogus) = nil error, want error")
	}
}
