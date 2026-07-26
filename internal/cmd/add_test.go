package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"cup/internal/project"
)

// A C++23 project that declines the std module — `std_module = false`, the shape
// cup's own C++ port under cpp/ records — must keep getting global-module-fragment
// sources at the newer standard. `import std;` needs GCC 15 and CMake 3.30, and the
// whole point of the setting is to stay on a GCC 14 floor while std::expected and
// std::println are available.
func TestAddWithoutStdModule(t *testing.T) {
	pinScaffoldEnv(t)
	root := scaffoldProject(t, goldenCell{project.ToolCMake, 23})

	// `cup new` has no picker for it yet, so record it the way cpp/cup.toml does.
	proj, err := project.Find()
	if err != nil {
		t.Fatalf("Find: %v", err)
	}
	cfg := proj.Config
	no := false
	cfg.StdModule = &no
	if err := project.WriteConfig(root, cfg); err != nil {
		t.Fatalf("WriteConfig: %v", err)
	}

	feed(t, "runner\n\n")
	if err := RunAdd([]string{"app"}); err != nil {
		t.Fatalf("cup add app: %v", err)
	}
	feed(t, addLibFeed(0, "lib_class"))
	if err := RunAdd([]string{"lib"}); err != nil {
		t.Fatalf("cup add lib: %v", err)
	}

	// The app is a plain translation unit: a #include, not an import.
	app := filepath.Join(root, "src", "apps", "runner", "runner.cpp")
	assertFile(t, app, "#include <print>")
	assertFile(t, app, "std::println")

	// The partition carries the standard library in a global module fragment, and
	// the aggregator carries one too (GCC 14 rejects the BMI otherwise).
	assertFile(t, filepath.Join(root, "src", "libs", "lib_class", "Lib_class.cppm"),
		"module;\n#include <print>\n")
	assertFile(t, filepath.Join(root, "src", "libs", "lib_class", "lib_class.cppm"),
		"module;\n#include <string>\n")

	// Nothing anywhere may reach for the std module.
	for _, rel := range []string{
		filepath.Join("src", "apps", "runner", "runner.cpp"),
		filepath.Join("src", "libs", "lib_class", "Lib_class.cppm"),
		filepath.Join("src", "libs", "lib_class", "lib_class.cppm"),
	} {
		b, err := os.ReadFile(filepath.Join(root, rel))
		if err != nil {
			t.Fatalf("reading %s: %v", rel, err)
		}
		if strings.Contains(string(b), "import std;") {
			t.Errorf("%s uses `import std;` with std_module = false:\n%s", rel, b)
		}
	}
}

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

// readFile returns a file's contents or fails the test.
func readFile(t *testing.T, path string) string {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(b)
}

// Under Make, `cup add app` writes only the app's own source file — no CMakeLists,
// and the root Makefile is left byte-for-byte unchanged (no rebase churn).
func TestAddAppMakeNoSharedEdit(t *testing.T) {
	proj := newMakeProject(t, 17)
	makefileBefore := readFile(t, filepath.Join(proj.Root, "Makefile"))

	feed(t, "greeter\n\n") // name, default filename
	if err := addApp(proj); err != nil {
		t.Fatalf("addApp(make): %v", err)
	}
	appDir := filepath.Join(proj.Src(), "apps", "greeter")
	assertFile(t, filepath.Join(appDir, "greeter.cpp"), "namespace")
	if isFile(filepath.Join(appDir, "CMakeLists.txt")) {
		t.Error("Make app should not have a CMakeLists.txt")
	}
	if isFile(filepath.Join(proj.Src(), "apps", "CMakeLists.txt")) {
		t.Error("Make add should not create src/apps/CMakeLists.txt")
	}
	if readFile(t, filepath.Join(proj.Root, "Makefile")) != makefileBefore {
		t.Error("root Makefile was modified by `cup add app` under Make")
	}
}

// Under Make, `cup add lib` writes the component sources and the lib's aggregator
// header, but no CMakeLists and no parent registration.
func TestAddLibMakeNoSharedEdit(t *testing.T) {
	proj := newMakeProject(t, 17)
	makefileBefore := readFile(t, filepath.Join(proj.Root, "Makefile"))

	// pickOrNew (no libs -> Text name "math"), chooseKind (class = option 1), symbol default.
	feed(t, "math\n1\n\n")
	if err := addLib(proj); err != nil {
		t.Fatalf("addLib(make): %v", err)
	}
	libDir := filepath.Join(proj.Src(), "libs", "math")
	assertFile(t, filepath.Join(libDir, "Math.h"), "class Math")
	assertFile(t, filepath.Join(libDir, "Math.cpp"), "namespace")
	assertFile(t, filepath.Join(libDir, "math.hpp"), "#include")
	if isFile(filepath.Join(libDir, "CMakeLists.txt")) {
		t.Error("Make lib should not have a CMakeLists.txt")
	}
	if isFile(filepath.Join(proj.Src(), "libs", "CMakeLists.txt")) {
		t.Error("Make add should not create src/libs/CMakeLists.txt")
	}
	if readFile(t, filepath.Join(proj.Root, "Makefile")) != makefileBefore {
		t.Error("root Makefile was modified by `cup add lib` under Make")
	}
}

// Under Make, `cup add test` writes only the test source; discovery + link-all
// wiring lives in the Makefile.
func TestAddTestMakeNoSharedEdit(t *testing.T) {
	proj := newMakeProject(t, 17)
	feed(t, "smoke\n") // no libs -> no module prompt; just the name
	if err := addTest(proj); err != nil {
		t.Fatalf("addTest(make): %v", err)
	}
	assertFile(t, filepath.Join(proj.Src(), "tests", "smoke.cpp"), "int main")
	if isFile(filepath.Join(proj.Src(), "tests", "CMakeLists.txt")) {
		t.Error("Make test should not create src/tests/CMakeLists.txt")
	}
}
