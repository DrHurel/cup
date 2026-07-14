package cmd

import (
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

func TestRemoveDownloadAbsent(t *testing.T) {
	proj := newProject(t, 23)
	if err := prepareThirdParty(proj); err != nil {
		t.Fatalf("prepareThirdParty: %v", err)
	}
	if err := removeDownload(proj, "ghost"); err == nil {
		t.Error("removeDownload(absent) = nil error, want error")
	}
}
