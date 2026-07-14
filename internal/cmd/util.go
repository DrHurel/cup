package cmd

import (
	"fmt"
	"os"
	"os/exec"

	"cup/internal/ui"
)

// version is stamped into cup.toml when a project is created.
const version = "0.1.0"

func isDir(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.IsDir()
}

func isFile(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

// runCommand runs an external command from dir with inherited stdio, so
// interactive sub-prompts (a sudo password, a git credential helper) still reach
// the terminal. A non-zero exit is returned as an error. It is a var so tests can
// stub out the git/docker/apt calls the register and image flows make.
var runCommand = func(dir string, name string, args ...string) error {
	ui.Running(name + " " + join(args))
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("command failed: %s %s: %w", name, join(args), err)
	}
	return nil
}

func join(args []string) string {
	out := ""
	for i, a := range args {
		if i > 0 {
			out += " "
		}
		out += a
	}
	return out
}
