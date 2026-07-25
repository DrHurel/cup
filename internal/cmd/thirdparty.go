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

// thirdPartyPath is the vendored-dependency prefix, e.g. third_party/<name>.
const thirdPartyPath = "third_party/"

const thirdPartyHeader = `# Third-party dependencies, registered via ` + "`cup register`" + `.
# git submodules -> add_subdirectory, cmake downloads -> FetchContent,
# system packages -> find_package.

include(FetchContent)
`

func thirdPartyCmake(proj *project.Project) string {
	return proj.Path("third_party", cmakelists)
}

// thirdPartyMake is the Make analogue of third_party/CMakeLists.txt: a fragment
// the root Makefile `-include`s. It carries CUP_TP_INCLUDES / CUP_TP_LIBS and the
// `# cup-dep:` / `# cup-apt:` markers cup uses to track registrations.
func thirdPartyMake(proj *project.Project) string {
	return proj.Path("third_party", "third_party.mk")
}

// thirdPartyFile returns the file registrations are recorded in for the project's
// build tool: third_party/CMakeLists.txt for CMake, third_party/third_party.mk for
// Make. aptPackages scans whichever, so Docker image sync works for both.
func thirdPartyFile(proj *project.Project) string {
	if proj.UsesMake() {
		return thirdPartyMake(proj)
	}
	return thirdPartyCmake(proj)
}

const thirdPartyMakeHeader = "# Third-party dependencies, registered via `cup register`.\n" +
	"# Included by the root Makefile. Add extra include flags to CUP_TP_INCLUDES and\n" +
	"# linker flags to CUP_TP_LIBS. cup appends tracking markers to each entry below\n" +
	"# so it can unregister them; edit entries but leave those markers intact.\n\n"

// cupDepMarker tags a Make CUP_TP_INCLUDES line with the method and name of the
// dependency that added it, so discoverDependencies/unregister can recover it.
const cupDepMarker = "# cup-dep:"

// prepareThirdParty ensures the third-party file for the build tool exists (and,
// for CMake, that the root build includes it before src/libs so dependencies
// configure first). The Make root Makefile already `-include`s third_party.mk, so
// no shared-file edit is needed there.
func prepareThirdParty(proj *project.Project) error {
	if proj.UsesMake() {
		return scaffold.EnsureFile(proj.Root, thirdPartyMake(proj), thirdPartyMakeHeader)
	}
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
	gitArgs = append(gitArgs, url, thirdPartyPath+name)
	if err := runCommand(proj.Root, "git", gitArgs...); err != nil {
		return err
	}
	if err := prepareThirdParty(proj); err != nil {
		return err
	}
	if proj.UsesMake() {
		return registerMakeDep(proj, methodSubmodule, name)
	}
	return scaffold.EnsureLine(proj.Root, thirdPartyCmake(proj), fmt.Sprintf("add_subdirectory(%s)", name))
}

// registerMakeDep records a vendored dependency in third_party.mk: it adds the
// dependency's directory to the compiler include path and tags the line with the
// method + name so `cup unregister` can unwind it. Header-and-source layouts vary,
// so it points the include at both third_party/<name> and its conventional
// include/ subdir; extra flags go in CUP_TP_LIBS by hand.
func registerMakeDep(proj *project.Project, method, name string) error {
	line := fmt.Sprintf("CUP_TP_INCLUDES += -Ithird_party/%s -Ithird_party/%s/include  %s %s %s",
		name, name, cupDepMarker, method, name)
	return scaffold.EnsureLine(proj.Root, thirdPartyMake(proj), line)
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
	// Make has no FetchContent to fetch at configure time, so vendor the sources now
	// with a shallow clone and expose their headers via third_party.mk.
	if proj.UsesMake() {
		if err := runCommand(proj.Root, "git", "clone", "--depth", "1",
			"--branch", tag, url, thirdPartyPath+name); err != nil {
			return err
		}
		if err := prepareThirdParty(proj); err != nil {
			return err
		}
		return registerMakeDep(proj, methodDownload, name)
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
	if proj.UsesMake() {
		return registerAptMake(proj)
	}
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
	// Tag the line with the apt package name so the build image can reinstall it
	// (the find_package name and the apt package name often differ, e.g.
	// find_package(Boost) <- apt libboost-dev).
	line := fmt.Sprintf("find_package(%s REQUIRED) %s %s", name, aptMarker, pkg)
	if err := scaffold.EnsureLine(proj.Root, thirdPartyCmake(proj), line); err != nil {
		return err
	}
	return syncDefaultBuildImage(proj)
}

// registerAptMake records an apt dependency for a Make project. There is no
// find_package to write, so it only tags third_party.mk with the package name so
// the default build image installs it (via aptPackages / syncDefaultBuildImage).
func registerAptMake(proj *project.Project) error {
	pkg, err := ui.Text("apt package name?", "", scaffold.ValidateNonEmpty)
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
	if err := scaffold.EnsureLine(proj.Root, thirdPartyMake(proj), aptMarker+" "+pkg); err != nil {
		return err
	}
	return syncDefaultBuildImage(proj)
}

// aptMarker tags a find_package line in third_party/CMakeLists.txt with the apt
// package that provides it, so aptPackages can reconstruct the install list the
// default build image needs.
const aptMarker = "# cup-apt:"

// aptPackages returns the apt package names recorded on the find_package lines of
// third_party/CMakeLists.txt (each apt registration tags its line with
// "# cup-apt: <pkg>"), in registration order and de-duplicated.
func aptPackages(proj *project.Project) []string {
	lines, ok := scaffold.ReadFileLines(thirdPartyFile(proj))
	if !ok {
		return nil
	}
	var pkgs []string
	seen := map[string]bool{}
	for _, raw := range lines {
		idx := strings.Index(raw, aptMarker)
		if idx < 0 {
			continue
		}
		for _, p := range strings.Fields(raw[idx+len(aptMarker):]) {
			if !seen[p] {
				seen[p] = true
				pkgs = append(pkgs, p)
			}
		}
	}
	return pkgs
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
	if proj.UsesMake() {
		return discoverMakeDependencies(proj)
	}
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

// cupDepRe captures the method and name off a Make `# cup-dep: <method> <name>`
// marker (submodule / download registrations).
var cupDepRe = regexp.MustCompile(cupDepMarker + `\s+(\S+)\s+(\S+)`)

// discoverMakeDependencies reads registrations back out of third_party.mk: vendored
// deps carry a `# cup-dep: <method> <name>` marker, apt packages a `# cup-apt:
// <pkg>` one (the pkg doubling as the dependency name).
func discoverMakeDependencies(proj *project.Project) []dependency {
	lines, ok := scaffold.ReadFileLines(thirdPartyMake(proj))
	if !ok {
		return nil
	}
	var deps []dependency
	for _, raw := range lines {
		if m := cupDepRe.FindStringSubmatch(raw); m != nil {
			deps = append(deps, dependency{name: m[2], method: m[1]})
			continue
		}
		if idx := strings.Index(raw, aptMarker); idx >= 0 {
			for _, p := range strings.Fields(raw[idx+len(aptMarker):]) {
				deps = append(deps, dependency{name: p, method: methodApt})
			}
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
	subPath := thirdPartyPath + name
	if err := runCommand(proj.Root, "git", "submodule", "deinit", "-f", subPath); err != nil {
		return err
	}
	if err := runCommand(proj.Root, "git", "rm", "-f", subPath); err != nil {
		return err
	}
	scaffold.RemoveDir(proj.Path(".git", "modules", "third_party", name))
	if proj.UsesMake() {
		_, err := removeMakeDepLine(proj, methodSubmodule, name)
		return err
	}
	_, err := scaffold.RemoveLine(proj.Root, thirdPartyCmake(proj), fmt.Sprintf("add_subdirectory(%s)", name))
	return err
}

func removeDownload(proj *project.Project, name string) error {
	if proj.UsesMake() {
		scaffold.RemoveDir(proj.Path("third_party", name))
		removed, err := removeMakeDepLine(proj, methodDownload, name)
		if err != nil {
			return err
		}
		if !removed {
			return fmt.Errorf("no download dependency %q found in third_party/third_party.mk", name)
		}
		return nil
	}
	removed, err := scaffold.RemoveFetchContentBlock(proj.Root, thirdPartyCmake(proj), name)
	if err != nil {
		return err
	}
	if !removed {
		return fmt.Errorf("no FetchContent block for %q found in third_party/%s", name, cmakelists)
	}
	return nil
}

// removeMakeDepLine drops the `# cup-dep: <method> <name>` CUP_TP_INCLUDES line
// that registerMakeDep wrote, reporting whether one was found.
func removeMakeDepLine(proj *project.Project, method, name string) (bool, error) {
	pattern := regexp.MustCompile(regexp.QuoteMeta(cupDepMarker) + `\s+` +
		regexp.QuoteMeta(method) + `\s+` + regexp.QuoteMeta(name) + `\b`)
	return scaffold.RemoveMatchingLine(proj.Root, thirdPartyMake(proj), pattern)
}

func removeApt(proj *project.Project, name string) error {
	pattern := regexp.MustCompile(`find_package\(\s*` + regexp.QuoteMeta(name) + `\b`)
	msg := fmt.Sprintf("no find_package(%s ...) line found in third_party/%s", name, cmakelists)
	if proj.UsesMake() {
		// The apt marker line is `# cup-apt: <pkg>`; the pkg is the dependency name.
		pattern = regexp.MustCompile(regexp.QuoteMeta(aptMarker) + `.*\b` + regexp.QuoteMeta(name) + `\b`)
		msg = fmt.Sprintf("no apt dependency %q found in third_party/third_party.mk", name)
	}
	removed, err := scaffold.RemoveMatchingLine(proj.Root, thirdPartyFile(proj), pattern)
	if err != nil {
		return err
	}
	if !removed {
		return fmt.Errorf("%s", msg)
	}
	if err := syncDefaultBuildImage(proj); err != nil {
		return err
	}
	ui.Skipped(fmt.Sprintf("the apt package for %s is left installed; remove it with apt if unwanted", name))
	return nil
}
