package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/ui"
)

// RunNew bootstraps a new thin cup project: a cup.toml marker, the C++23-modules
// root CMakeLists.txt, the src/{apps,libs} tree, a .gitignore, and a git repo.
// The project carries no build tooling of its own — a globally installed cup
// manages it.
func RunNew(args []string) error {
	name, err := resolveProjectName(args)
	if err != nil {
		return err
	}

	tool, err := chooseBuildTool()
	if err != nil {
		return err
	}

	std, err := chooseStandard(tool)
	if err != nil {
		return err
	}

	gcc, clang, err := chooseCompilerFloors(std)
	if err != nil {
		return err
	}

	base, err := chooseBaseImage()
	if err != nil {
		return err
	}

	root, err := filepath.Abs(name)
	if err != nil {
		return err
	}
	if _, err := os.Stat(root); err == nil {
		return fmt.Errorf("%s already exists", name)
	}

	// The default build image shares the project's (lowercased) name; cup keeps its
	// docker/<name>/Dockerfile in sync with the project's apt dependencies.
	proj := &project.Project{Root: root, Config: project.Config{
		Name:        name,
		CupVersion:  version,
		CppStandard: std,
		BuildTool:   tool,
		Compiler:    project.NewCompilerConfig(gcc, clang),
		Docker: project.DockerConfig{Images: []project.DockerImage{
			{Name: strings.ToLower(name), Base: base, Default: true},
		}},
	}}

	// The marker must exist before the scaffold helpers, which resolve paths and
	// template overrides relative to the project root, can log against it.
	if err := os.MkdirAll(root, 0o755); err != nil {
		return err
	}
	if err := project.WriteConfig(root, proj.Config); err != nil {
		return err
	}
	ui.Wrote(project.Marker)

	if err := scaffoldProjectTree(proj, std, gcc, clang); err != nil {
		return err
	}

	if err := runCommand(root, "git", "init", "-q"); err != nil {
		ui.Skipped("git init failed; initialise the repository yourself")
	}

	ui.Success("done.")
	ui.Next(fmt.Sprintf("cd %s", name))
	ui.Next("cup add app     # scaffold your first executable")
	ui.Next("cup build       # compile (Debug)")
	ui.Next("cup docker build   # build the toolchain image")
	return nil
}

// chooseBuildTool asks which build system to scaffold. CMake (the default) drives
// C++11–23 (headers or modules); Make targets the headers family (C++11/14/17)
// with discovery-based Makefiles that stay conflict-free on rebase.
func chooseBuildTool() (string, error) {
	return ui.Select("build system?",
		[]string{project.ToolCMake, project.ToolMake}, project.ToolCMake)
}

// standardChoices returns the C++ standards offered for a build tool: all of them
// for CMake, only the headers family (C++11/14/17) for Make, which cannot robustly
// build C++20/23 modules.
func standardChoices(tool string) []int {
	if tool != project.ToolMake {
		return scaffold.Standards
	}
	var out []int
	for _, s := range scaffold.Standards {
		if !scaffold.UsesModules(s) {
			out = append(out, s)
		}
	}
	return out
}

// scaffoldProjectTree writes the files a fresh project needs beyond its cup.toml
// marker: the root CMakeLists, .gitignore, the empty src/{apps,libs} CMakeLists,
// and the default build image's Dockerfile.
func scaffoldProjectTree(proj *project.Project, std, gcc, clang int) error {
	if proj.UsesMake() {
		return scaffoldMakeTree(proj, std)
	}
	root, family := proj.Root, scaffold.Family(std)
	rootCmake, err := scaffold.Render(root, family, "project", "CMakeLists.txt.tmpl", map[string]string{
		"name":             proj.Config.Name,
		"standard":         fmt.Sprintf("%d", std),
		"module_std_setup": moduleStdSetup(proj.Config),
		"compiler_guard":   scaffold.CompilerGuard(gcc, clang),
	})
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(root, filepath.Join(root, cmakelists), rootCmake); err != nil {
		return err
	}

	gitignore, err := scaffold.Render(root, family, "project", "gitignore.tmpl", nil)
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(root, filepath.Join(root, ".gitignore"), gitignore); err != nil {
		return err
	}

	// src/apps and src/libs are always add_subdirectory'd by the root CMakeLists,
	// so each needs a (initially empty) CMakeLists.txt to exist from the start.
	for _, sub := range []string{"apps", "libs"} {
		if err := scaffold.EnsureFile(root, filepath.Join(root, "src", sub, cmakelists), ""); err != nil {
			return err
		}
	}

	// Generate the default build image's docker/<name>/Dockerfile (just `FROM base`
	// until dependencies are registered).
	return syncDefaultBuildImage(proj)
}

// scaffoldMakeTree writes a Make-backed project's files: the discovery-based root
// Makefile, a Make-flavoured .gitignore, the empty src/{apps,libs} directories the
// Makefile globs over, and the default build image's Dockerfile. No per-directory
// build files are written — the Makefile finds components by path — so `cup add`
// never has to touch a shared file.
func scaffoldMakeTree(proj *project.Project, std int) error {
	root := proj.Root
	makefile, err := scaffold.Render(root, project.ToolMake, "project", "Makefile.tmpl", map[string]string{
		"name":       proj.Config.Name,
		"std_number": fmt.Sprintf("%d", std),
	})
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(root, filepath.Join(root, "Makefile"), makefile); err != nil {
		return err
	}

	gitignore, err := scaffold.Render(root, project.ToolMake, "project", "gitignore.tmpl", nil)
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(root, filepath.Join(root, ".gitignore"), gitignore); err != nil {
		return err
	}

	// src/apps and src/libs need to exist so the Makefile's find(1)-based discovery
	// has somewhere to look; unlike CMake they carry no CMakeLists.txt.
	for _, sub := range []string{"apps", "libs"} {
		if err := os.MkdirAll(filepath.Join(proj.Src(), sub), 0o755); err != nil {
			return err
		}
	}

	return syncDefaultBuildImage(proj)
}

// resolveProjectName takes the project name from args or prompts for it, then
// validates it as a C++ identifier.
func resolveProjectName(args []string) (string, error) {
	name := ""
	if len(args) > 0 {
		name = args[0]
	} else {
		var err error
		name, err = ui.Text("project name?", "", scaffold.ValidateIdent)
		if err != nil {
			return "", err
		}
	}
	if err := scaffold.ValidateIdent(name); err != nil {
		return "", fmt.Errorf("project name %q: %w", name, err)
	}
	return name, nil
}

// chooseStandard asks which C++ standard the project targets, defaulting to the
// newest offered for the build tool. The choice decides everything downstream:
// C++20/23 scaffold modules, C++11/14/17 scaffold classic headers. Make offers
// only the headers standards (see standardChoices).
func chooseStandard(tool string) (int, error) {
	stds := standardChoices(tool)
	labels := make([]string, len(stds))
	for i, s := range stds {
		labels[i] = scaffold.StdLabel(s)
	}
	choice, err := ui.Select("c++ standard?", labels, labels[0])
	if err != nil {
		return 0, err
	}
	return scaffold.ParseStd(choice)
}

// chooseCompilerFloors asks which compilers to pin a minimum for — GCC, Clang,
// or both — then, for each chosen one, which version (offering only releases that
// can build the standard; C++23 needs GCC 15, so it is the sole GCC option). An
// unchosen compiler stays 0 (no floor) and is left out of cup.toml's [compiler]
// table and the CMakeLists guard entirely.
func chooseCompilerFloors(std int) (gcc, clang int, err error) {
	const (
		both      = "gcc and clang"
		gccOnly   = "gcc only"
		clangOnly = "clang only"
	)
	which, err := ui.Select("pin a minimum version for which compilers?",
		[]string{both, gccOnly, clangOnly}, both)
	if err != nil {
		return 0, 0, err
	}

	newestGCC, newestClang := scaffold.NewestCompilers()
	gccChoices, clangChoices := scaffold.CompilerChoices(std, newestGCC, newestClang)

	if which != clangOnly {
		if gcc, err = chooseCompilerFloor("gcc", gccChoices); err != nil {
			return 0, 0, err
		}
	}
	if which != gccOnly {
		if clang, err = chooseCompilerFloor("clang", clangChoices); err != nil {
			return 0, 0, err
		}
	}
	return gcc, clang, nil
}

// chooseCompilerFloor asks for one compiler's minimum version. choices is
// oldest-first; the oldest (most permissive floor) is the default. A lone choice
// is taken without prompting.
func chooseCompilerFloor(name string, choices []int) (int, error) {
	if len(choices) == 1 {
		return choices[0], nil
	}
	labels := make([]string, len(choices))
	for i, v := range choices {
		labels[i] = strconv.Itoa(v)
	}
	choice, err := ui.Select("minimum "+name+" version?", labels, labels[0])
	if err != nil {
		return 0, err
	}
	return strconv.Atoi(choice)
}

// moduleStdSetup returns the top-of-file CMake block for the modules family: the
// minimum-version line plus, for a project on the std module, the experimental
// `import std` opt-in. Without it there is only the version floor — C++20 has no
// std module, and a C++23 project can opt out of it (`std_module = false`) to stay
// on a GCC 14 floor. It is unused (and empty) for the headers family.
func moduleStdSetup(cfg project.Config) string {
	if cfg.UsesStdModule() {
		return `# ` + "`import std;`" + ` requires CMake >= 3.30 (CMAKE_CXX_MODULE_STD support for GCC)
# and a compiler that ships the std-module manifest (GCC 15+).
cmake_minimum_required(VERSION 3.30)

# Opt in to CMake's still-experimental ` + "`import std`" + ` support. The gate value is a
# CMake-version-specific UUID that must match exactly; this one is for CMake 4.4.
# Bump it if you move to another CMake.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

# Build and provide the ` + "`std`" + ` module to every C++23 target so sources can
# ` + "`import std;`" + `. This MUST be set before CXX is enabled by project(): compiler
# support for the std module is detected there and skipped if it is off.
set(CMAKE_CXX_MODULE_STD ON)
`
	}
	// Named modules alone need CMake >= 3.28, whatever the standard.
	return `# Named modules need CMake >= 3.28.
cmake_minimum_required(VERSION 3.28)
`
}
