package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/ui"
)

// RunNew bootstraps a new thin cup project: a cup.toml marker, the C++23-modules
// root CMakeLists.txt, the src/{apps,libs} tree, a .gitignore, and a git repo.
// The project carries no build tooling of its own — a globally installed cup
// manages it.
func RunNew(args []string) error {
	var name string
	var err error
	if len(args) > 0 {
		name = args[0]
	} else {
		name, err = ui.Text("project name?", "", scaffold.ValidateIdent)
		if err != nil {
			return err
		}
	}
	if err := scaffold.ValidateIdent(name); err != nil {
		return fmt.Errorf("project name %q: %w", name, err)
	}

	std, err := chooseStandard()
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

	// The marker must exist before the scaffold helpers, which resolve paths and
	// template overrides relative to the project root, can log against it.
	if err := os.MkdirAll(root, 0o755); err != nil {
		return err
	}
	if err := project.WriteConfig(root, project.Config{Name: name, CupVersion: version, CppStandard: std}); err != nil {
		return err
	}
	ui.Wrote(project.Marker)

	family := scaffold.Family(std)
	rootCmake, err := scaffold.Render(root, family, "project", "CMakeLists.txt.tmpl", map[string]string{
		"name":             name,
		"standard":         fmt.Sprintf("%d", std),
		"module_std_setup": moduleStdSetup(std),
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

	if err := runCommand(root, "git", "init", "-q"); err != nil {
		ui.Skipped("git init failed; initialise the repository yourself")
	}

	ui.Success("done.")
	ui.Next(fmt.Sprintf("cd %s", name))
	ui.Next("cup add app     # scaffold your first executable")
	ui.Next("cup build       # configure + compile (Debug)")
	return nil
}

// chooseStandard asks which C++ standard the project targets, defaulting to the
// newest. The choice decides everything downstream: C++20/23 scaffold modules,
// C++11/14/17 scaffold classic headers.
func chooseStandard() (int, error) {
	labels := make([]string, len(scaffold.Standards))
	for i, s := range scaffold.Standards {
		labels[i] = scaffold.StdLabel(s)
	}
	choice, err := ui.Select("c++ standard?", labels, labels[0])
	if err != nil {
		return 0, err
	}
	return scaffold.ParseStd(choice)
}

// moduleStdSetup returns the top-of-file CMake block for the modules family: the
// minimum-version line plus, on C++23, the experimental `import std` opt-in.
// C++20 modules don't use the std module, so they get only the version floor.
// It is unused (and empty) for the headers family.
func moduleStdSetup(std int) string {
	if std >= 23 {
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
	// C++20 named modules need CMake >= 3.28; no std module.
	return `# Named modules need CMake >= 3.28.
cmake_minimum_required(VERSION 3.28)
`
}
