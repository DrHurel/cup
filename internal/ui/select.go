package ui

import (
	"fmt"
	"os"

	"golang.org/x/term"
)

// Select shows an arrow-key menu and returns the chosen option. def, if it
// matches an option, is pre-highlighted. When stdin is not a terminal it falls
// back to a numbered list read from a line, so cup still works over a pipe.
func Select(question string, options []string, def string) (string, error) {
	if len(options) == 0 {
		return "", fmt.Errorf("no options to choose from")
	}
	fd := int(os.Stdin.Fd())
	if !term.IsTerminal(fd) {
		return selectNumbered(question, options)
	}

	cursor := 0
	for i, o := range options {
		if o == def {
			cursor = i
			break
		}
	}

	oldState, err := term.MakeRaw(fd)
	if err != nil {
		return selectNumbered(question, options)
	}
	defer term.Restore(fd, oldState)

	fmt.Printf("%s %s %s\r\n", color(cyan+";1", "?"), question, color(grey, "(↑/↓, enter)"))
	render := func() {
		for i, o := range options {
			pointer := "  "
			line := o
			if i == cursor {
				pointer = color(cyan+";1", "❯ ")
				line = color(cyan+";1", o)
			}
			fmt.Printf("%s%s\r\n", pointer, line)
		}
	}
	render()

	buf := make([]byte, 3)
	for {
		n, err := os.Stdin.Read(buf)
		if err != nil || n == 0 {
			return "", ErrAbort
		}
		switch {
		case buf[0] == 3 || buf[0] == 4: // Ctrl+C / Ctrl+D
			return "", ErrAbort
		case buf[0] == '\r' || buf[0] == '\n':
			moveUp(len(options))
			redrawFinal(options, cursor)
			return options[cursor], nil
		case buf[0] == 'k' || (n == 3 && buf[0] == 27 && buf[2] == 'A'): // up
			cursor = (cursor - 1 + len(options)) % len(options)
		case buf[0] == 'j' || (n == 3 && buf[0] == 27 && buf[2] == 'B'): // down
			cursor = (cursor + 1) % len(options)
		default:
			continue
		}
		moveUp(len(options))
		render()
	}
}

func moveUp(n int) {
	for i := 0; i < n; i++ {
		fmt.Print("\x1b[1A\x1b[2K") // up one line, clear it
	}
}

// redrawFinal reprints the list once selection is made, marking the pick, so the
// terminal shows a stable record after raw mode is restored.
func redrawFinal(options []string, cursor int) {
	for i, o := range options {
		if i == cursor {
			fmt.Printf("%s%s\r\n", color(green+";1", "❯ "), color(green+";1", o))
		} else {
			fmt.Printf("  %s\r\n", color(grey, o))
		}
	}
}

func selectNumbered(question string, options []string) (string, error) {
	fmt.Println(question)
	for i, o := range options {
		fmt.Printf("  %d) %s\n", i+1, o)
	}
	for {
		choice, err := Text("choice number?", "1", nil)
		if err != nil {
			return "", err
		}
		var idx int
		if _, err := fmt.Sscanf(choice, "%d", &idx); err == nil && idx >= 1 && idx <= len(options) {
			return options[idx-1], nil
		}
	}
}
