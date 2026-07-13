package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/ui"
)

var buildModes = []string{"Debug", "Release", "Coverage"}

// parseMode peels an optional leading build-mode argument off args, defaulting to
// Debug. The remaining args are returned for the caller (e.g. run's app name).
func parseMode(args []string) (string, []string) {
	if len(args) > 0 {
		for _, m := range buildModes {
			if args[0] == m {
				return m, args[1:]
			}
		}
	}
	return "Debug", args
}

func buildDir(proj *project.Project, mode string) string {
	return proj.Path("build", mode)
}

// Configure generates the CMake build system for mode under build/<mode>.
func Configure(proj *project.Project, mode string) error {
	return runCommand(proj.Root, "cmake",
		"-G", "Ninja",
		"-DCMAKE_BUILD_TYPE="+mode,
		"-S", proj.Root,
		"-B", buildDir(proj, mode),
	)
}

// Build configures (if needed) then compiles mode.
func Build(proj *project.Project, mode string) error {
	if err := Configure(proj, mode); err != nil {
		return err
	}
	return runCommand(proj.Root, "cmake", "--build", buildDir(proj, mode))
}

func RunConfigure(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	mode, _ := parseMode(args)
	return Configure(proj, mode)
}

func RunBuild(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	mode, _ := parseMode(args)
	return Build(proj, mode)
}

func RunTest(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	mode, _ := parseMode(args)
	if err := Build(proj, mode); err != nil {
		return err
	}
	return runCommand(proj.Root, "ctest", "--test-dir", buildDir(proj, mode), "--output-on-failure")
}

// RunRun builds then runs an app binary: `cup run [MODE] [app] [-- args...]`.
func RunRun(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	mode, rest := parseMode(args)

	appName, progArgs, err := resolveApp(proj, rest)
	if err != nil {
		return err
	}
	if err := Build(proj, mode); err != nil {
		return err
	}
	bin := filepath.Join(buildDir(proj, mode), "bin", appName)
	return runCommand(proj.Root, bin, progArgs...)
}

// resolveApp picks the app to run and separates its program arguments. A leading
// non-"--" token names the app; otherwise the sole app is used, or the user is
// asked to choose. A "--" separates cup's args from the program's.
func resolveApp(proj *project.Project, rest []string) (string, []string, error) {
	apps := scaffold.ListSubdirs(filepath.Join(proj.Src(), "apps"))
	if len(apps) == 0 {
		return "", nil, fmt.Errorf("no apps to run (add one with `cup add app`)")
	}

	var appName string
	if len(rest) > 0 && rest[0] != "--" {
		appName = rest[0]
		rest = rest[1:]
	} else if len(apps) == 1 {
		appName = apps[0]
	} else {
		chosen, err := ui.Select("which app?", apps, apps[0])
		if err != nil {
			return "", nil, err
		}
		appName = chosen
	}
	if len(rest) > 0 && rest[0] == "--" {
		rest = rest[1:]
	}
	return appName, rest, nil
}

// RunClean removes the entire build/ tree.
func RunClean(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	if err := os.RemoveAll(proj.Path("build")); err != nil {
		return err
	}
	ui.Removed("build/")
	return nil
}

func RunRebuild(args []string) error {
	if err := RunClean(nil); err != nil {
		return err
	}
	return RunBuild(args)
}

func RunRetest(args []string) error {
	if err := RunClean(nil); err != nil {
		return err
	}
	return RunTest(args)
}
