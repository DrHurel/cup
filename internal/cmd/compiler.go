package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/ui"
)

// RunCompiler is `cup compiler`: show or change the project's minimum compiler
// versions. Lowering or raising a floor is docker-verified — cup compiles the
// project in the configured toolchain image and reverts the change if the build
// fails, so a compiler floor can never drift away from what actually compiles.
//
//	cup compiler                       show the current floors and verify image
//	cup compiler set gcc|clang <ver>   change a floor (docker-verified)
//	cup compiler verify                compile the project in the toolchain image
//
// The set/verify flows accept --image REF to override cup.toml's verify_image;
// set also accepts --no-verify to skip the docker check.
func RunCompiler(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	if len(args) == 0 || args[0] == "show" {
		return showCompilers(proj)
	}
	switch args[0] {
	case "set":
		return setCompiler(proj, args[1:])
	case "verify":
		return verifyCompiler(proj, args[1:])
	default:
		return fmt.Errorf("unknown `cup compiler` subcommand %q (use: show, set, verify)", args[0])
	}
}

// effectiveCompilers returns the project's minimum GCC and Clang major versions,
// falling back to cup's per-standard defaults when cup.toml pins none (older
// projects predate the [compiler] table).
func effectiveCompilers(cfg project.Config) (gcc, clang int) {
	if cfg.Compiler.HasFloor() {
		return cfg.Compiler.GCCFloor(), cfg.Compiler.ClangFloor()
	}
	return scaffold.MinCompilers(cfg.Standard())
}

func showCompilers(proj *project.Project) error {
	gcc, clang := effectiveCompilers(proj.Config)
	ui.Accent("minimum compiler versions")
	fmt.Println("  gcc     " + floorLabel(gcc))
	fmt.Println("  clang   " + floorLabel(clang))
	image := proj.Config.Compiler.VerifyImage
	if image == "" {
		image = "(unset — set verify_image in cup.toml or pass --image)"
	}
	fmt.Println("  verify  " + image)
	return nil
}

// floorLabel renders a minimum version for display; a zero means the compiler is
// not gated.
func floorLabel(v int) string {
	if v == 0 {
		return "(no floor)"
	}
	return ">= " + strconv.Itoa(v)
}

// setCompiler changes one compiler floor, then docker-compiles the project to
// verify the new floor. If the build fails, cup.toml and the root CMakeLists are
// restored byte-for-byte so the change is fully cancelled.
func setCompiler(proj *project.Project, args []string) error {
	image, noVerify, rest, err := parseCompilerFlags(args)
	if err != nil {
		return err
	}
	name, ver, cfg, err := planCompilerChange(proj.Config, rest)
	if err != nil {
		return err
	}

	if image == "" {
		image = cfg.Compiler.VerifyImage
	}
	if !noVerify && image == "" {
		return fmt.Errorf("no docker image to verify against: set verify_image in cup.toml, pass --image REF, or use --no-verify to skip the check")
	}

	if err := commitCompilerFloor(proj, cfg, image, noVerify); err != nil {
		return err
	}
	suffix := "."
	if noVerify {
		suffix = " (unverified)."
	}
	ui.Success(fmt.Sprintf("done — %s minimum is now %s%s", name, floorLabel(ver), suffix))
	return nil
}

// planCompilerChange validates the `set` positional args and returns the compiler
// changed, its new version, and the resulting config. It materialises the
// effective floors first so setting one compiler never silently drops the
// other's default.
func planCompilerChange(cur project.Config, rest []string) (name string, ver int, cfg project.Config, err error) {
	if len(rest) != 2 {
		return "", 0, cfg, fmt.Errorf("usage: cup compiler set <gcc|clang> <version> [--image REF] [--no-verify]")
	}
	name = rest[0]
	if name != "gcc" && name != "clang" {
		return "", 0, cfg, fmt.Errorf("unknown compiler %q (use: gcc, clang)", name)
	}
	ver, err = strconv.Atoi(rest[1])
	if err != nil || ver < 0 {
		return "", 0, cfg, fmt.Errorf("invalid version %q: want a non-negative major version like 15", rest[1])
	}

	gcc, clang := effectiveCompilers(cur)
	if name == "gcc" {
		gcc = ver
	} else {
		clang = ver
	}
	cfg = cur
	cfg.Compiler = project.NewCompilerConfig(gcc, clang)
	cfg.Compiler.VerifyImage = cur.Compiler.VerifyImage // preserve the verify image
	return name, ver, cfg, nil
}

// commitCompilerFloor writes the new floor to cup.toml and the CMake guard, then
// (unless noVerify) docker-compiles the project. Any failure restores both files
// byte-for-byte, so the change is all-or-nothing.
func commitCompilerFloor(proj *project.Project, cfg project.Config, image string, noVerify bool) error {
	tomlPath := proj.Path(project.Marker)
	cmakePath := proj.Path(cmakelists)
	oldToml, errT := os.ReadFile(tomlPath)
	oldCmake, errC := os.ReadFile(cmakePath)
	if errT != nil || errC != nil {
		return fmt.Errorf("cannot snapshot project files before changing the compiler floor")
	}
	restore := func() {
		_ = os.WriteFile(tomlPath, oldToml, 0o644)
		_ = os.WriteFile(cmakePath, oldCmake, 0o644)
	}

	// A partial write (e.g. cup.toml updated but the CMakeLists has no guard
	// markers to rewrite) must not leave the two files disagreeing.
	if err := applyCompilerFloor(proj.Root, cfg); err != nil {
		restore()
		return err
	}
	if noVerify {
		ui.Skipped("docker verification (--no-verify)")
		return nil
	}

	ui.Running("verifying the project still compiles with " + image)
	if err := dockerVerify(proj.Root, image); err != nil {
		restore()
		ui.Err("compiler change cancelled: the project did not compile with " + image)
		return err
	}
	return nil
}

// verifyCompiler compiles the project in the toolchain image without changing
// anything — a standalone `does it still build?` check.
func verifyCompiler(proj *project.Project, args []string) error {
	image, _, rest, err := parseCompilerFlags(args)
	if err != nil {
		return err
	}
	if len(rest) != 0 {
		return fmt.Errorf("usage: cup compiler verify [--image REF]")
	}
	if image == "" {
		image = proj.Config.Compiler.VerifyImage
	}
	if image == "" {
		return fmt.Errorf("no docker image to verify against: set verify_image in cup.toml or pass --image REF")
	}
	ui.Running("compiling the project in " + image)
	if err := dockerVerify(proj.Root, image); err != nil {
		return err
	}
	ui.Success("ok — the project compiles in " + image + ".")
	return nil
}

// applyCompilerFloor writes cfg back to cup.toml and rewrites the root
// CMakeLists' compiler-guard block to match.
func applyCompilerFloor(root string, cfg project.Config) error {
	if err := project.WriteConfig(root, cfg); err != nil {
		return err
	}
	ui.Updated(project.Marker + "  (compiler floor)")
	return scaffold.ReplaceCompilerGuard(root, filepath.Join(root, cmakelists),
		cfg.Compiler.GCCFloor(), cfg.Compiler.ClangFloor())
}

// dockerVerify compiles the project inside a docker image to prove it still
// builds on a given toolchain. The source tree is mounted read-only and the
// build runs in a container-local directory, so the check can neither mutate nor
// litter the project. A non-zero build exits as an error.
func dockerVerify(root, image string) error {
	const script = "cmake -S /work -B /tmp/cup-verify -G Ninja && cmake --build /tmp/cup-verify"
	return runCommand(root, "docker", "run", "--rm",
		"-v", root+":/work:ro",
		image, "sh", "-c", script)
}

// parseCompilerFlags peels the --image REF and --no-verify options out of args,
// returning them alongside the remaining positional arguments.
func parseCompilerFlags(args []string) (image string, noVerify bool, rest []string, err error) {
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--no-verify":
			noVerify = true
		case "--image":
			if i+1 >= len(args) {
				return "", false, nil, fmt.Errorf("--image needs a docker image reference")
			}
			i++
			image = args[i]
		default:
			rest = append(rest, args[i])
		}
	}
	return image, noVerify, rest, nil
}
