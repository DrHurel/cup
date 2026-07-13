package cmd

import (
	"fmt"
	"path/filepath"
	"regexp"
	"strings"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/ui"
)

const (
	methodSubmodule = "git-submodule"
	methodDownload  = "cmake-download"
	methodApt       = "apt-install"
)

var thirdPartyMethods = []string{methodSubmodule, methodDownload, methodApt}

const thirdPartyHeader = `# Third-party dependencies, registered via ` + "`cup register`" + `.
# git submodules -> add_subdirectory, cmake downloads -> FetchContent,
# system packages -> find_package.

include(FetchContent)
`

func thirdPartyCmake(proj *project.Project) string {
	return proj.Path("third_party", cmakelists)
}

// prepareThirdParty ensures third_party/CMakeLists.txt exists and that the root
// build includes it before src/libs, so dependencies configure first.
func prepareThirdParty(proj *project.Project) error {
	if err := scaffold.EnsureFile(proj.Root, thirdPartyCmake(proj), thirdPartyHeader); err != nil {
		return err
	}
	return scaffold.EnsureLineBefore(proj.Root, filepath.Join(proj.Root, cmakelists),
		"add_subdirectory(third_party)", "add_subdirectory(src/libs)")
}

// RunRegister is the `cup register` entrypoint: vendor a third-party dependency
// via git submodule, CMake FetchContent, or an apt package.
func RunRegister(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	method, err := ui.Select("how should the dependency be fetched?", thirdPartyMethods, methodSubmodule)
	if err != nil {
		return err
	}
	switch method {
	case methodSubmodule:
		return registerSubmodule(proj)
	case methodDownload:
		return registerDownload(proj)
	case methodApt:
		return registerApt(proj)
	default:
		return fmt.Errorf("unknown method: %q", method)
	}
}

func registerSubmodule(proj *project.Project) error {
	name, err := ui.Text("dependency name?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	url, err := ui.Text("git repository URL?", "", scaffold.ValidateNonEmpty)
	if err != nil {
		return err
	}
	ref, err := ui.Text("branch or tag? (blank for the default branch)", "", nil)
	if err != nil {
		return err
	}
	gitArgs := []string{"submodule", "add"}
	if ref != "" {
		gitArgs = append(gitArgs, "--branch", ref)
	}
	gitArgs = append(gitArgs, url, "third_party/"+name)
	if err := runCommand(proj.Root, "git", gitArgs...); err != nil {
		return err
	}
	if err := prepareThirdParty(proj); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root, thirdPartyCmake(proj), fmt.Sprintf("add_subdirectory(%s)", name))
}

func registerDownload(proj *project.Project) error {
	name, err := ui.Text("dependency name?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	url, err := ui.Text("git repository URL?", "", scaffold.ValidateNonEmpty)
	if err != nil {
		return err
	}
	tag, err := ui.Text("git tag / ref?", "", scaffold.ValidateNonEmpty)
	if err != nil {
		return err
	}
	block := fmt.Sprintf("FetchContent_Declare(\n  %s\n  GIT_REPOSITORY %s\n  GIT_TAG %s\n)\nFetchContent_MakeAvailable(%s)\n",
		name, url, tag, name)
	if err := prepareThirdParty(proj); err != nil {
		return err
	}
	return scaffold.AppendBlock(proj.Root, thirdPartyCmake(proj),
		fmt.Sprintf("FetchContent_MakeAvailable(%s)", name), block)
}

func registerApt(proj *project.Project) error {
	name, err := ui.Text("find_package name?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	pkg, err := ui.Text("apt package name?", strings.ToLower(name), scaffold.ValidateNonEmpty)
	if err != nil {
		return err
	}
	install, err := ui.Confirm(fmt.Sprintf("run 'sudo apt-get install -y %s' now?", pkg), true)
	if err != nil {
		return err
	}
	if err := prepareThirdParty(proj); err != nil {
		return err
	}
	if install {
		if err := runCommand(proj.Root, "sudo", "apt-get", "install", "-y", pkg); err != nil {
			return err
		}
	}
	return scaffold.EnsureLine(proj.Root, thirdPartyCmake(proj), fmt.Sprintf("find_package(%s REQUIRED)", name))
}

// --- unregister ------------------------------------------------------------

type dependency struct {
	name   string
	method string
}

var (
	submoduleRe   = regexp.MustCompile(`add_subdirectory\(\s*([A-Za-z0-9_./-]+)\s*\)`)
	downloadRe    = regexp.MustCompile(`FetchContent_MakeAvailable\(\s*([A-Za-z0-9_]+)\s*\)`)
	findPackageRe = regexp.MustCompile(`find_package\(\s*([A-Za-z0-9_]+)`)
)

func discoverDependencies(proj *project.Project) []dependency {
	lines, ok := scaffold.ReadFileLines(thirdPartyCmake(proj))
	if !ok {
		return nil
	}
	var deps []dependency
	for _, raw := range lines {
		line := strings.TrimSpace(raw)
		if m := submoduleRe.FindStringSubmatch(line); m != nil && strings.HasPrefix(line, "add_subdirectory") {
			deps = append(deps, dependency{m[1], methodSubmodule})
		} else if m := downloadRe.FindStringSubmatch(line); m != nil && strings.HasPrefix(line, "FetchContent_MakeAvailable") {
			deps = append(deps, dependency{m[1], methodDownload})
		} else if m := findPackageRe.FindStringSubmatch(line); m != nil && strings.HasPrefix(line, "find_package") {
			deps = append(deps, dependency{m[1], methodApt})
		}
	}
	return deps
}

// RunUnregister is the `cup unregister [name]` entrypoint: unwind whatever the
// matching registration wrote.
func RunUnregister(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	deps := discoverDependencies(proj)
	if len(deps) == 0 {
		ui.Accent("no third-party dependencies registered — nothing to remove.")
		return nil
	}

	dep, err := resolveDependency(deps, args)
	if err != nil {
		return err
	}
	ok, err := ui.Confirm(fmt.Sprintf("remove %s (%s)?", dep.name, dep.method), false)
	if err != nil {
		return err
	}
	if !ok {
		ui.Skipped(dep.name)
		return nil
	}
	switch dep.method {
	case methodSubmodule:
		return removeSubmodule(proj, dep.name)
	case methodDownload:
		return removeDownload(proj, dep.name)
	case methodApt:
		return removeApt(proj, dep.name)
	default:
		return fmt.Errorf("unknown method: %q", dep.method)
	}
}

func resolveDependency(deps []dependency, args []string) (dependency, error) {
	byName := map[string]dependency{}
	var names []string
	for _, d := range deps {
		byName[d.name] = d
		names = append(names, d.name)
	}
	if len(args) > 0 {
		d, ok := byName[args[0]]
		if !ok {
			return dependency{}, fmt.Errorf("no registered dependency named %q. Known: %s",
				args[0], strings.Join(names, ", "))
		}
		return d, nil
	}
	labels := make([]string, len(deps))
	for i, d := range deps {
		labels[i] = fmt.Sprintf("%s  (%s)", d.name, d.method)
	}
	picked, err := ui.Select("which dependency should be removed?", labels, labels[0])
	if err != nil {
		return dependency{}, err
	}
	return byName[strings.SplitN(picked, "  (", 2)[0]], nil
}

func removeSubmodule(proj *project.Project, name string) error {
	subPath := "third_party/" + name
	if err := runCommand(proj.Root, "git", "submodule", "deinit", "-f", subPath); err != nil {
		return err
	}
	if err := runCommand(proj.Root, "git", "rm", "-f", subPath); err != nil {
		return err
	}
	scaffold.RemoveDir(proj.Path(".git", "modules", "third_party", name))
	_, err := scaffold.RemoveLine(proj.Root, thirdPartyCmake(proj), fmt.Sprintf("add_subdirectory(%s)", name))
	return err
}

func removeDownload(proj *project.Project, name string) error {
	removed, err := scaffold.RemoveFetchContentBlock(proj.Root, thirdPartyCmake(proj), name)
	if err != nil {
		return err
	}
	if !removed {
		return fmt.Errorf("no FetchContent block for %q found in third_party/%s", name, cmakelists)
	}
	return nil
}

func removeApt(proj *project.Project, name string) error {
	removed, err := scaffold.RemoveMatchingLine(proj.Root, thirdPartyCmake(proj),
		regexp.MustCompile(`find_package\(\s*`+regexp.QuoteMeta(name)+`\b`))
	if err != nil {
		return err
	}
	if !removed {
		return fmt.Errorf("no find_package(%s ...) line found in third_party/%s", name, cmakelists)
	}
	ui.Skipped(fmt.Sprintf("the apt package for %s is left installed; remove it with apt if unwanted", name))
	return nil
}
