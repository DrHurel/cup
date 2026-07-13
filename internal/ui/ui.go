// Package ui provides the interactive prompts and coloured status output cup
// uses while scaffolding — an arrow-key Select, a y/n Confirm, and a validated
// Text input, plus the "wrote / updated / skipped" log lines.
package ui

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"golang.org/x/term"
)

// ErrAbort is returned by every prompt when the user aborts with Ctrl+C or EOF.
// Commands translate it into a clean "aborted." exit rather than an error.
var ErrAbort = errors.New("aborted")

// --- colours ---------------------------------------------------------------

var useColor = os.Getenv("NO_COLOR") == "" && term.IsTerminal(int(os.Stdout.Fd()))

func color(code, s string) string {
	if !useColor {
		return s
	}
	return "\x1b[" + code + "m" + s + "\x1b[0m"
}

const (
	cyan   = "38;5;81"
	green  = "38;5;77"
	orange = "38;5;215"
	grey   = "38;5;244"
	red    = "38;5;203;1"
	salmon = "38;5;209"
)

// --- status lines ----------------------------------------------------------

func Running(msg string) { fmt.Println("  " + color(cyan, "run     ") + " " + msg) }
func Wrote(msg string)   { fmt.Println("  " + color(green, "wrote   ") + " " + msg) }
func Updated(msg string) { fmt.Println("  " + color(orange, "updated ") + " " + msg) }
func Skipped(msg string) { fmt.Println("  " + color(grey, "skipped ") + " " + msg) }
func Removed(msg string) { fmt.Println("  " + color(salmon, "removed ") + " " + msg) }
func Next(msg string)    { fmt.Println("  " + color(cyan, "next    ") + " " + msg) }

func Accent(msg string)  { fmt.Println(color(cyan+";1", msg)) }
func Success(msg string) { fmt.Println(color(green+";1", msg)) }
func Err(msg string)     { fmt.Fprintln(os.Stderr, color(red, msg)) }

// --- text input ------------------------------------------------------------

var stdin = bufio.NewReader(os.Stdin)

// SetInput redirects the reader the prompts read from and returns a function that
// restores the previous reader. It lets callers drive the prompts from a scripted
// source (a pipe, a test) instead of the terminal.
func SetInput(r io.Reader) func() {
	prev := stdin
	stdin = bufio.NewReader(r)
	return func() { stdin = prev }
}

// Text prompts for a line of input. An empty entry falls back to def. validate,
// if non-nil, must return nil to accept the value; otherwise its error is shown
// and the prompt repeats.
func Text(question, def string, validate func(string) error) (string, error) {
	for {
		if def != "" {
			fmt.Printf("%s %s [%s] ", color(cyan+";1", "?"), question, color(grey, def))
		} else {
			fmt.Printf("%s %s ", color(cyan+";1", "?"), question)
		}
		line, err := stdin.ReadString('\n')
		if err != nil && line == "" {
			fmt.Println()
			return "", ErrAbort
		}
		value := strings.TrimSpace(line)
		if value == "" {
			value = def
		}
		if validate != nil {
			if verr := validate(value); verr != nil {
				Err("  " + verr.Error())
				continue
			}
		}
		return value, nil
	}
}

// Confirm asks a yes/no question, returning def on an empty answer.
func Confirm(question string, def bool) (bool, error) {
	hint := "y/N"
	if def {
		hint = "Y/n"
	}
	for {
		fmt.Printf("%s %s [%s] ", color(cyan+";1", "?"), question, hint)
		line, err := stdin.ReadString('\n')
		if err != nil && line == "" {
			fmt.Println()
			return false, ErrAbort
		}
		switch strings.ToLower(strings.TrimSpace(line)) {
		case "":
			return def, nil
		case "y", "yes":
			return true, nil
		case "n", "no":
			return false, nil
		}
	}
}
