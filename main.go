// Command cup scaffolds and manages C++23-modules projects: a single binary that
// creates thin projects (`cup new`), scaffolds apps/libs/tests into them
// (`cup add`), wires in third-party dependencies (`cup register`), and drives the
// CMake/Ninja build (`cup build`, `cup run`, `cup test`).
package main

import (
	"errors"
	"fmt"
	"os"

	"cup/internal/cmd"
	"cup/internal/ui"
)

func main() {
	args := os.Args[1:]
	if len(args) == 0 || args[0] == "-h" || args[0] == "--help" || args[0] == "help" {
		usage()
		return
	}
	name, rest := args[0], args[1:]
	for _, c := range cmd.Commands {
		if c.Name == name {
			if err := c.Run(rest); err != nil {
				if errors.Is(err, ui.ErrAbort) {
					ui.Err("aborted.")
				} else {
					ui.Err("error: " + err.Error())
				}
				os.Exit(1)
			}
			return
		}
	}
	ui.Err(fmt.Sprintf("unknown command %q", name))
	usage()
	os.Exit(1)
}

func usage() {
	fmt.Println("cup — scaffold and manage C++23-modules projects")
	fmt.Println()
	fmt.Println("usage: cup <command> [args]")
	fmt.Println()
	fmt.Println("commands:")
	for _, c := range cmd.Commands {
		fmt.Printf("  %-11s %s\n", c.Name, c.Summary)
	}
	fmt.Println()
	fmt.Println("MODE is one of Debug (default), Release, or Coverage; each gets its own build/<MODE> tree.")
}
