package cmd

import (
	"fmt"
	"path/filepath"
	"strings"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/tmpl"
	"cup/internal/ui"
)

const (
	cmakelists   = "CMakeLists.txt"
	newSentinel  = "[new…]"
	noneSentinel = "[none]"
)

var categories = []string{"app", "lib", "test", "third-party"}

// family returns the template family (modules / headers) for the project's
// chosen C++ standard.
func family(proj *project.Project) string {
	return scaffold.Family(proj.Config.Standard())
}

// stdVars builds the variable map passed to scaffold.Render: the per-standard
// values (std_lib, std_number, hello, …) merged with the given key/value pairs.
func stdVars(proj *project.Project, kv ...string) map[string]string {
	vars := scaffold.StdVars(proj.Config.Standard())
	for i := 0; i+1 < len(kv); i += 2 {
		vars[kv[i]] = kv[i+1]
	}
	return vars
}

// RunAdd is the `cup add` entrypoint. With a category argument it scaffolds that
// one target; without one it prompts, then offers to add another.
func RunAdd(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}

	if len(args) > 0 {
		return dispatchCategory(proj, args[0])
	}

	for {
		category, err := ui.Select("what do you want to add?", categories, "app")
		if err != nil {
			return err
		}
		if err := dispatchCategory(proj, category); err != nil {
			return err
		}
		ui.Success("done.")
		again, err := ui.Confirm("add another?", true)
		if err != nil || !again {
			return nil
		}
		fmt.Println()
	}
}

func dispatchCategory(proj *project.Project, category string) error {
	switch category {
	case "app":
		return addApp(proj)
	case "lib":
		return addLib(proj)
	case "test":
		return addTest(proj)
	case "third-party":
		return RunRegister(nil)
	default:
		return fmt.Errorf("unknown category: %q", category)
	}
}

// pickOrNew offers existing options plus a "[new…]" entry; picking it (or having
// no options) prompts for a fresh name.
func pickOrNew(question string, options []string, newPrompt string, validate func(string) error) (string, error) {
	if len(options) == 0 {
		return ui.Text(newPrompt, "", validate)
	}
	choice, err := ui.Select(question, append(append([]string{}, options...), newSentinel), options[0])
	if err != nil {
		return "", err
	}
	if choice == newSentinel {
		return ui.Text(newPrompt, "", validate)
	}
	return choice, nil
}

func addApp(proj *project.Project) error {
	name, err := ui.Text("app name?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	filename, err := ui.Text("source filename?", name+".cpp", nil)
	if err != nil {
		return err
	}
	appDir := filepath.Join(proj.Src(), "apps", name)
	namespace := scaffold.PathToNamespace(proj.Src(), appDir)
	fam := family(proj)

	src, err := scaffold.Render(proj.Root, fam, "app", "source.cpp.tmpl",
		stdVars(proj, "name", name, "namespace", namespace))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, filepath.Join(appDir, filename), src); err != nil {
		return err
	}
	cml, err := scaffold.Render(proj.Root, fam, "app", "CMakeLists.txt.tmpl",
		stdVars(proj, "name", name, "filename", filename))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, filepath.Join(appDir, cmakelists), cml); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root,
		filepath.Join(proj.Src(), "apps", cmakelists), fmt.Sprintf("add_subdirectory(%s)", name))
}

func addLib(proj *project.Project) error {
	libsDir := filepath.Join(proj.Src(), "libs")
	existing := scaffold.ListSubdirs(libsDir)
	name, err := pickOrNew("lib name?", existing, "new lib name?", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	libDir := filepath.Join(libsDir, name)
	if isDir(libDir) {
		return extendLib(proj, libDir)
	}
	return createLibAt(proj, name, libDir, filepath.Join(libsDir, cmakelists))
}

// createLibAt scaffolds a new lib target and registers it with its parent. The
// primary symbol lives in its own partition file (<Symbol>.cppm -> module
// <module>:<partition>); the lib's primary interface (<name>.cppm) is a thin
// aggregator that re-exports that partition.
func createLibAt(proj *project.Project, name, targetDir, parentCmake string) error {
	if !proj.UsesModules() {
		return createHeaderLibAt(proj, name, targetDir, parentCmake)
	}
	kind, err := chooseKind(proj.Root, family(proj))
	if err != nil {
		return err
	}
	module := scaffold.PathToModule(proj.Src(), targetDir)
	symbol, err := ui.Text("primary symbol name?", scaffold.Capitalize(name), scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	namespace := scaffold.PathToNamespace(proj.Src(), targetDir)
	partition := strings.ToLower(symbol)
	primary := filepath.Join(targetDir, name+".cppm")
	cmake := filepath.Join(targetDir, cmakelists)

	src, err := scaffold.Render(proj.Root, "modules", kind, "source.cppm.tmpl",
		stdVars(proj, "module", module+":"+partition, "symbol", symbol, "namespace", namespace))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, filepath.Join(targetDir, symbol+".cppm"), src); err != nil {
		return err
	}
	if err := writePrimaryAggregator(proj, primary, module, partition); err != nil {
		return err
	}
	cml, err := scaffold.Render(proj.Root, "modules", kind, "CMakeLists.txt.tmpl", stdVars(proj, "name", name))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, cmake, cml); err != nil {
		return err
	}
	if err := scaffold.AddModuleSource(proj.Root, cmake, symbol+".cppm"); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root, parentCmake, fmt.Sprintf("add_subdirectory(%s)", name))
}

// writePrimaryAggregator creates a lib's primary interface unit as a thin
// aggregator over one partition. A declined overwrite leaves the existing primary
// untouched.
func writePrimaryAggregator(proj *project.Project, primary, module, partition string) error {
	wrote, err := scaffold.WriteFile(proj.Root, primary, fmt.Sprintf("export module %s;\n", module))
	if err != nil || !wrote {
		return err
	}
	return scaffold.AddPartitionImport(proj.Root, primary, partition)
}

func addFileToLib(proj *project.Project, libDir string) error {
	if !proj.UsesModules() {
		return addFileToHeaderLib(proj, libDir)
	}
	filename, err := ui.Text("new file name (no extension)?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	kind, err := chooseKind(proj.Root, family(proj))
	if err != nil {
		return err
	}
	module := scaffold.PathToModule(proj.Src(), libDir) + ":" + filename
	symbol, err := ui.Text("primary symbol name?", scaffold.Capitalize(filename), scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	namespace := scaffold.PathToNamespace(proj.Src(), libDir)
	primary := filepath.Join(libDir, filepath.Base(libDir)+".cppm")

	src, err := scaffold.Render(proj.Root, "modules", kind, "source.cppm.tmpl",
		stdVars(proj, "module", module, "symbol", symbol, "namespace", namespace))
	if err != nil {
		return err
	}
	wrote, err := scaffold.WriteFile(proj.Root, filepath.Join(libDir, filename+".cppm"), src)
	if err != nil || !wrote {
		return err
	}
	if err := scaffold.AddModuleSource(proj.Root, filepath.Join(libDir, cmakelists), filename+".cppm"); err != nil {
		return err
	}
	return scaffold.AddPartitionImport(proj.Root, primary, filename)
}

func extendLib(proj *project.Project, libDir string) error {
	what, err := ui.Select(fmt.Sprintf("add to '%s' as?", relTo(proj.Root, libDir)),
		[]string{"file", "subfolder"}, "file")
	if err != nil {
		return err
	}
	if what == "file" {
		return addFileToLib(proj, libDir)
	}
	var existingSubs []string
	for _, sub := range scaffold.ListSubdirs(libDir) {
		if isFile(filepath.Join(libDir, sub, cmakelists)) {
			existingSubs = append(existingSubs, sub)
		}
	}
	sub, err := pickOrNew("subfolder name?", existingSubs, "new subfolder name?", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	subDir := filepath.Join(libDir, sub)
	if isDir(subDir) {
		return extendLib(proj, subDir)
	}
	return createLibAt(proj, sub, subDir, filepath.Join(libDir, cmakelists))
}

// chooseTestModule prompts for which library the test exercises, returning "" when
// no libraries exist or the user picks "none".
func chooseTestModule(proj *project.Project) (string, error) {
	libs := scaffold.ListSubdirs(filepath.Join(proj.Src(), "libs"))
	if len(libs) == 0 {
		return "", nil
	}
	picked, err := ui.Select("module under test?", append([]string{noneSentinel}, libs...), noneSentinel)
	if err != nil {
		return "", err
	}
	if picked == noneSentinel {
		return "", nil
	}
	return picked, nil
}

// testModuleImport returns the top-of-file line that pulls in the module under
// test — a C++ module import or a classic header include — or "" when there is none.
func testModuleImport(proj *project.Project, module string) string {
	if module == "" {
		return ""
	}
	if proj.UsesModules() {
		return fmt.Sprintf("import %s;\n", module)
	}
	return fmt.Sprintf("#include \"%s.hpp\"\n", module)
}

func addTest(proj *project.Project) error {
	name, err := ui.Text("test name?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	module, err := chooseTestModule(proj)
	if err != nil {
		return err
	}
	moduleImport := testModuleImport(proj, module)
	testsDir := filepath.Join(proj.Src(), "tests")
	namespace := scaffold.PathToNamespace(proj.Src(), testsDir)
	if namespace == "" {
		namespace = name
	}
	src, err := scaffold.Render(proj.Root, family(proj), "test", "source.cpp.tmpl",
		stdVars(proj, "name", name, "module_import", moduleImport, "namespace", namespace))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, filepath.Join(testsDir, name+".cpp"), src); err != nil {
		return err
	}

	testsCmake := filepath.Join(testsDir, cmakelists)
	steps := []string{
		fmt.Sprintf("add_executable(%s %s.cpp)", name, name),
		fmt.Sprintf("target_compile_features(%s PRIVATE cxx_std_%d)", name, proj.Config.Standard()),
	}
	if module != "" {
		steps = append(steps, fmt.Sprintf("target_link_libraries(%s PRIVATE %s)", name, module))
	}
	steps = append(steps, fmt.Sprintf("add_test(NAME %s COMMAND %s)", name, name))
	for _, line := range steps {
		if err := scaffold.EnsureLine(proj.Root, testsCmake, line); err != nil {
			return err
		}
	}
	return scaffold.EnsureLine(proj.Root, filepath.Join(proj.Root, cmakelists), "add_subdirectory(src/tests)")
}

// chooseKind prompts for a library-component template kind, defaulting to "class"
// when available.
func chooseKind(root, family string) (string, error) {
	kinds := tmpl.Kinds(root, family)
	if len(kinds) == 0 {
		return "", fmt.Errorf("no library templates available")
	}
	def := kinds[0]
	for _, k := range kinds {
		if k == "class" {
			def = "class"
			break
		}
	}
	return ui.Select("template kind?", kinds, def)
}

func relTo(root, path string) string {
	if r, err := filepath.Rel(root, path); err == nil {
		return r
	}
	return path
}
