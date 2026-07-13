package main

import (
	"os"
	"testing"
)

// The paths through main that do not call os.Exit are safe to invoke directly:
// the help/empty cases and a subcommand that succeeds (completion bash prints a
// script and returns nil).
func TestMainNoExitPaths(t *testing.T) {
	prev := os.Args
	t.Cleanup(func() { os.Args = prev })

	for _, args := range [][]string{
		{"cup"},
		{"cup", "help"},
		{"cup", "-h"},
		{"cup", "--help"},
		{"cup", "completion", "bash"},
	} {
		os.Args = args
		main() // must not panic or exit
	}
}

func TestUsage(t *testing.T) {
	// usage prints the banner and every command; it must not panic.
	usage()
}
