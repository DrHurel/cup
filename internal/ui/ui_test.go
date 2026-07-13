package ui

import (
	"bufio"
	"errors"
	"fmt"
	"strings"
	"testing"
)

// feedStdin replaces the package-level stdin reader with input for the duration
// of the test, restoring it afterwards.
func feedStdin(t *testing.T, input string) {
	t.Helper()
	prev := stdin
	stdin = bufio.NewReader(strings.NewReader(input))
	t.Cleanup(func() { stdin = prev })
}

func TestColor(t *testing.T) {
	// useColor depends on the environment; force both branches deterministically.
	prev := useColor
	t.Cleanup(func() { useColor = prev })

	useColor = false
	if got := color(cyan, "hi"); got != "hi" {
		t.Errorf("color (no color) = %q, want %q", got, "hi")
	}

	useColor = true
	if got := color("1", "hi"); got != "\x1b[1mhi\x1b[0m" {
		t.Errorf("color (with color) = %q, want escapes", got)
	}
}

func TestTextUsesDefaultOnEmpty(t *testing.T) {
	feedStdin(t, "\n")
	got, err := Text("name?", "fallback", nil)
	if err != nil {
		t.Fatalf("Text error: %v", err)
	}
	if got != "fallback" {
		t.Errorf("Text = %q, want default %q", got, "fallback")
	}
}

func TestTextTrimsAndReturnsInput(t *testing.T) {
	feedStdin(t, "  hello  \n")
	got, err := Text("name?", "def", nil)
	if err != nil {
		t.Fatalf("Text error: %v", err)
	}
	if got != "hello" {
		t.Errorf("Text = %q, want %q", got, "hello")
	}
}

func TestTextRepeatsUntilValid(t *testing.T) {
	feedStdin(t, "bad\ngood\n")
	validate := func(s string) error {
		if s != "good" {
			return errors.New("must be good")
		}
		return nil
	}
	got, err := Text("name?", "", validate)
	if err != nil {
		t.Fatalf("Text error: %v", err)
	}
	if got != "good" {
		t.Errorf("Text = %q, want %q after retry", got, "good")
	}
}

func TestTextAbortsOnEOF(t *testing.T) {
	feedStdin(t, "") // immediate EOF, no data
	_, err := Text("name?", "", nil)
	if !errors.Is(err, ErrAbort) {
		t.Errorf("Text on EOF = %v, want ErrAbort", err)
	}
}

func TestConfirm(t *testing.T) {
	cases := []struct {
		input string
		def   bool
		want  bool
	}{
		{"y\n", false, true},
		{"yes\n", false, true},
		{"n\n", true, false},
		{"no\n", true, false},
		{"Y\n", false, true},        // case-insensitive
		{"\n", true, true},          // empty -> default
		{"\n", false, false},        // empty -> default
		{"maybe\ny\n", false, true}, // unrecognized answer repeats
	}
	for _, c := range cases {
		feedStdin(t, c.input)
		got, err := Confirm("ok?", c.def)
		if err != nil {
			t.Fatalf("Confirm(%q) error: %v", c.input, err)
		}
		if got != c.want {
			t.Errorf("Confirm(%q, def=%v) = %v, want %v", c.input, c.def, got, c.want)
		}
	}
}

func TestConfirmAbortsOnEOF(t *testing.T) {
	feedStdin(t, "")
	_, err := Confirm("ok?", false)
	if !errors.Is(err, ErrAbort) {
		t.Errorf("Confirm on EOF = %v, want ErrAbort", err)
	}
}

// The status-line helpers must not panic and should emit the message text.
func TestStatusLinesDoNotPanic(t *testing.T) {
	fns := map[string]func(string){
		"Running": Running,
		"Wrote":   Wrote,
		"Updated": Updated,
		"Skipped": Skipped,
		"Removed": Removed,
		"Next":    Next,
		"Accent":  Accent,
		"Success": Success,
		"Err":     Err,
	}
	for name, fn := range fns {
		func() {
			defer func() {
				if r := recover(); r != nil {
					t.Errorf("%s panicked: %v", name, r)
				}
			}()
			fn(fmt.Sprintf("%s message", name))
		}()
	}
}
