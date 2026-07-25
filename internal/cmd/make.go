package cmd

import "cup/internal/project"

// This file holds the Make counterparts to the CMake build steps in build.go.
// The generated root Makefile takes MODE=<mode> and lays its output out under
// build/<mode>/{obj,lib,bin} — the same tree `cup` expects — so run/clean stay
// build-system-agnostic and only configure/build/test need to branch.

// makeBuild compiles the project with the generated Makefile for the given mode.
// There is no separate configure step: the Makefile discovers its components at
// invocation time.
func makeBuild(proj *project.Project, mode string) error {
	return runCommand(proj.Root, "make", "MODE="+mode)
}

// makeTest builds and runs every test under src/tests via the Makefile's `test`
// target (which links each test against the project's lib archives).
func makeTest(proj *project.Project, mode string) error {
	return runCommand(proj.Root, "make", "MODE="+mode, "test")
}
