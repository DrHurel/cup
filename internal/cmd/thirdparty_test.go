package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// registerDownload is pure filesystem work (no external tools), so the full
// FetchContent registration is exercisable end-to-end.
func TestRegisterDownloadAndDiscover(t *testing.T) {
	proj := newProject(t, 23)
	// name, url, tag
	feed(t, "fmt\nhttps://github.com/fmtlib/fmt\n10.2.1\n")
	if err := registerDownload(proj); err != nil {
		t.Fatalf("registerDownload: %v", err)
	}
	tpc := thirdPartyCmake(proj)
	assertFile(t, tpc, "FetchContent_Declare(")
	assertFile(t, tpc, "FetchContent_MakeAvailable(fmt)")
	// The root build now includes third_party before src/libs.
	assertFile(t, filepath.Join(proj.Root, "CMakeLists.txt"), "add_subdirectory(third_party)")

	deps := discoverDependencies(proj)
	if len(deps) != 1 || deps[0].name != "fmt" || deps[0].method != methodDownload {
		t.Fatalf("discoverDependencies = %+v", deps)
	}
}

func TestRunRegisterDispatchesDownload(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	// method Select: cmake-download is option 2; then name, url, tag.
	feed(t, "2\nspdlog\nhttps://github.com/gabime/spdlog\nv1.13.0\n")
	if err := RunRegister(nil); err != nil {
		t.Fatalf("RunRegister: %v", err)
	}
	assertFile(t, thirdPartyCmake(proj), "FetchContent_MakeAvailable(spdlog)")
}

func TestResolveDependency(t *testing.T) {
	deps := []dependency{{"fmt", methodDownload}, {"boost", methodApt}}

	// By explicit name.
	d, err := resolveDependency(deps, []string{"boost"})
	if err != nil || d.name != "boost" {
		t.Fatalf("resolveDependency(named) = %+v, %v", d, err)
	}

	// Unknown name errors.
	if _, err := resolveDependency(deps, []string{"nope"}); err == nil {
		t.Error("resolveDependency(unknown) = nil error, want error")
	}

	// Interactive: pick option 1 (fmt), matching the "name  (method)" label.
	feed(t, "1\n")
	d, err = resolveDependency(deps, nil)
	if err != nil || d.name != "fmt" {
		t.Fatalf("resolveDependency(prompt) = %+v, %v", d, err)
	}
}

func TestRunUnregisterDownload(t *testing.T) {
	proj := newProject(t, 23)
	feed(t, "fmt\nhttps://github.com/fmtlib/fmt\n10.2.1\n")
	if err := registerDownload(proj); err != nil {
		t.Fatalf("registerDownload: %v", err)
	}
	t.Chdir(proj.Root)
	// name fmt given as arg -> skip the picker; confirm yes.
	feed(t, "y\n")
	if err := RunUnregister([]string{"fmt"}); err != nil {
		t.Fatalf("RunUnregister: %v", err)
	}
	deps := discoverDependencies(proj)
	if len(deps) != 0 {
		t.Errorf("dependency still registered after unregister: %+v", deps)
	}
}

func TestRunUnregisterDeclined(t *testing.T) {
	proj := newProject(t, 23)
	feed(t, "fmt\nhttps://github.com/fmtlib/fmt\n10.2.1\n")
	if err := registerDownload(proj); err != nil {
		t.Fatalf("registerDownload: %v", err)
	}
	t.Chdir(proj.Root)
	feed(t, "n\n") // decline removal
	if err := RunUnregister([]string{"fmt"}); err != nil {
		t.Fatalf("RunUnregister(declined): %v", err)
	}
	if deps := discoverDependencies(proj); len(deps) != 1 {
		t.Errorf("declined removal should keep the dependency, got %+v", deps)
	}
}

func TestRunUnregisterNothingRegistered(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	if err := RunUnregister(nil); err != nil {
		t.Errorf("RunUnregister(empty) = %v, want nil", err)
	}
}

func TestRemoveApt(t *testing.T) {
	proj := newProject(t, 23)
	// Register an apt-style find_package line without running apt.
	feed(t, "Boost\nlibboost-dev\nn\n") // find_package name, apt pkg, decline install
	t.Chdir(proj.Root)
	if err := registerApt(proj); err != nil {
		t.Fatalf("registerApt: %v", err)
	}
	assertFile(t, thirdPartyCmake(proj), "find_package(Boost REQUIRED)")
	if err := removeApt(proj, "Boost"); err != nil {
		t.Fatalf("removeApt: %v", err)
	}
	if err := removeApt(proj, "Boost"); err == nil {
		t.Error("removeApt(absent) = nil error, want error")
	}
}

// Registering an apt dependency records its package on the find_package line and
// regenerates the default build image's Dockerfile to install it; unregistering
// drops the package but leaves the (now bare) Dockerfile in place.
func TestRegisterAptSyncsDefaultBuildImage(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	feed(t, "Boost\nlibboost-dev\nn\n") // find_package name, apt pkg, decline install
	t.Chdir(proj.Root)
	if err := registerApt(proj); err != nil {
		t.Fatalf("registerApt: %v", err)
	}
	// The apt package is tagged onto the find_package line so it survives reloads.
	assertFile(t, thirdPartyCmake(proj), "find_package(Boost REQUIRED) "+aptMarker+" libboost-dev")

	dockerfile := dockerfilePath(proj, "demo")
	assertFile(t, dockerfile, "FROM gcc:14")
	assertFile(t, dockerfile, "libboost-dev")

	if pkgs := aptPackages(proj); len(pkgs) != 1 || pkgs[0] != "libboost-dev" {
		t.Fatalf("aptPackages = %v, want [libboost-dev]", pkgs)
	}

	// Removing the dependency regenerates the Dockerfile without the package.
	if err := removeApt(proj, "Boost"); err != nil {
		t.Fatalf("removeApt: %v", err)
	}
	assertFile(t, dockerfile, "FROM gcc:14")
	b, _ := os.ReadFile(dockerfile)
	if strings.Contains(string(b), "libboost-dev") {
		t.Errorf("Dockerfile still installs libboost-dev after unregister:\n%s", b)
	}
}

// registerSubmodule shells out to git; with that stubbed the full registration is
// exercisable: it runs `git submodule add` (with --branch when a ref is given) and
// records add_subdirectory(name) in third_party/CMakeLists.txt.
func TestRegisterSubmodule(t *testing.T) {
	proj := newProject(t, 23)
	calls := stubRunCommand(t, nil)
	// name, url, ref
	feed(t, "cli11\nhttps://github.com/CLIUtils/CLI11\nv2.4.1\n")
	if err := registerSubmodule(proj); err != nil {
		t.Fatalf("registerSubmodule: %v", err)
	}
	if len(*calls) != 1 || !strings.Contains((*calls)[0], "git submodule add --branch v2.4.1 https://github.com/CLIUtils/CLI11 third_party/cli11") {
		t.Fatalf("git call = %v", *calls)
	}
	assertFile(t, thirdPartyCmake(proj), "add_subdirectory(cli11)")

	deps := discoverDependencies(proj)
	if len(deps) != 1 || deps[0].name != "cli11" || deps[0].method != methodSubmodule {
		t.Fatalf("discoverDependencies = %+v", deps)
	}
}

// Without a ref, `git submodule add` is invoked without --branch.
func TestRegisterSubmoduleNoRef(t *testing.T) {
	proj := newProject(t, 23)
	calls := stubRunCommand(t, nil)
	feed(t, "cli11\nhttps://github.com/CLIUtils/CLI11\n\n") // blank ref
	if err := registerSubmodule(proj); err != nil {
		t.Fatalf("registerSubmodule: %v", err)
	}
	if len(*calls) != 1 || strings.Contains((*calls)[0], "--branch") {
		t.Fatalf("git call unexpectedly used --branch: %v", *calls)
	}
}

// A failing `git submodule add` aborts before touching the CMake file.
func TestRegisterSubmoduleGitFails(t *testing.T) {
	proj := newProject(t, 23)
	stubRunCommand(t, func(name string, args []string) error {
		return fmt.Errorf("git boom")
	})
	feed(t, "cli11\nhttps://github.com/CLIUtils/CLI11\n\n")
	if err := registerSubmodule(proj); err == nil {
		t.Error("registerSubmodule(git fails) = nil error, want error")
	}
	if isFile(thirdPartyCmake(proj)) {
		t.Error("registerSubmodule wrote third_party/CMakeLists.txt despite git failure")
	}
}

// RunRegister routes the submodule choice (menu option 1) to registerSubmodule.
func TestRunRegisterDispatchesSubmodule(t *testing.T) {
	proj := newProject(t, 23)
	t.Chdir(proj.Root)
	stubRunCommand(t, nil)
	// method Select option 1 = git-submodule; then name, url, ref.
	feed(t, "1\ncli11\nhttps://github.com/CLIUtils/CLI11\n\n")
	if err := RunRegister(nil); err != nil {
		t.Fatalf("RunRegister: %v", err)
	}
	assertFile(t, thirdPartyCmake(proj), "add_subdirectory(cli11)")
}

// registerApt with the install confirmed runs `sudo apt-get install`.
func TestRegisterAptInstalls(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	calls := stubRunCommand(t, nil)
	t.Chdir(proj.Root)
	feed(t, "Boost\nlibboost-dev\ny\n") // find_package name, apt pkg, confirm install
	if err := registerApt(proj); err != nil {
		t.Fatalf("registerApt: %v", err)
	}
	if len(*calls) != 1 || (*calls)[0] != "sudo apt-get install -y libboost-dev" {
		t.Fatalf("apt call = %v", *calls)
	}
	assertFile(t, thirdPartyCmake(proj), "find_package(Boost REQUIRED) "+aptMarker+" libboost-dev")
}

// removeSubmodule unwinds a submodule registration: it deinits and `git rm`s the
// path and drops the add_subdirectory line.
func TestRemoveSubmodule(t *testing.T) {
	proj := newProject(t, 23)
	stubRunCommand(t, nil)
	feed(t, "cli11\nhttps://github.com/CLIUtils/CLI11\n\n")
	if err := registerSubmodule(proj); err != nil {
		t.Fatalf("registerSubmodule: %v", err)
	}
	calls := stubRunCommand(t, nil)
	if err := removeSubmodule(proj, "cli11"); err != nil {
		t.Fatalf("removeSubmodule: %v", err)
	}
	if len(*calls) != 2 ||
		!strings.Contains((*calls)[0], "git submodule deinit -f third_party/cli11") ||
		!strings.Contains((*calls)[1], "git rm -f third_party/cli11") {
		t.Fatalf("git calls = %v", *calls)
	}
	if deps := discoverDependencies(proj); len(deps) != 0 {
		t.Errorf("submodule still registered after removeSubmodule: %+v", deps)
	}
}

// A failing `git submodule deinit` surfaces the error.
func TestRemoveSubmoduleGitFails(t *testing.T) {
	proj := newProject(t, 23)
	stubRunCommand(t, func(name string, args []string) error { return fmt.Errorf("git boom") })
	if err := removeSubmodule(proj, "cli11"); err == nil {
		t.Error("removeSubmodule(git fails) = nil error, want error")
	}
}

func TestRemoveDownloadAbsent(t *testing.T) {
	proj := newProject(t, 23)
	if err := prepareThirdParty(proj); err != nil {
		t.Fatalf("prepareThirdParty: %v", err)
	}
	if err := removeDownload(proj, "ghost"); err == nil {
		t.Error("removeDownload(absent) = nil error, want error")
	}
}
