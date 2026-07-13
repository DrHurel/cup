package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSubcommandNames(t *testing.T) {
	names := subcommandNames()
	if len(names) != len(Commands) {
		t.Fatalf("subcommandNames len = %d, want %d", len(names), len(Commands))
	}
	joined := strings.Join(names, " ")
	for _, want := range []string{"new", "add", "build", "completion"} {
		if !strings.Contains(joined, want) {
			t.Errorf("subcommandNames missing %q: %v", want, names)
		}
	}
}

func TestScriptFor(t *testing.T) {
	for _, shell := range []string{"bash", "zsh", "fish"} {
		script, err := scriptFor(shell)
		if err != nil {
			t.Errorf("scriptFor(%s) error: %v", shell, err)
		}
		if script == "" {
			t.Errorf("scriptFor(%s) empty", shell)
		}
	}
	if _, err := scriptFor("powershell"); err == nil {
		t.Error("scriptFor(powershell) = nil error, want error")
	}
}

func TestCompletionScriptsMentionCommands(t *testing.T) {
	for name, gen := range map[string]func() string{
		"bash": bashCompletion,
		"zsh":  zshCompletion,
		"fish": fishCompletion,
	} {
		if !strings.Contains(gen(), "new") || !strings.Contains(gen(), "completion") {
			t.Errorf("%s completion missing subcommands", name)
		}
	}
}

func TestFishQuote(t *testing.T) {
	if got := fishQuote("a'b"); got != `'a\'b'` {
		t.Errorf("fishQuote = %q", got)
	}
}

func TestDetectShell(t *testing.T) {
	cases := map[string]string{
		"/usr/bin/zsh":  "zsh",
		"/bin/fish":     "fish",
		"/bin/bash":     "bash",
		"/usr/bin/dash": "bash", // unknown -> bash
		"":              "bash",
	}
	for shellPath, want := range cases {
		t.Setenv("SHELL", shellPath)
		if got := detectShell(); got != want {
			t.Errorf("detectShell(SHELL=%q) = %q, want %q", shellPath, got, want)
		}
	}
}

func TestDataHome(t *testing.T) {
	t.Setenv("XDG_DATA_HOME", "/custom/data")
	if got := dataHome("/home/u"); got != "/custom/data" {
		t.Errorf("dataHome(XDG set) = %q", got)
	}
	t.Setenv("XDG_DATA_HOME", "")
	if got := dataHome("/home/u"); got != filepath.Join("/home/u", ".local", "share") {
		t.Errorf("dataHome(default) = %q", got)
	}
}

func TestRunCompletionPrintsAndErrors(t *testing.T) {
	for _, shell := range []string{"bash", "zsh", "fish"} {
		if err := RunCompletion([]string{shell}); err != nil {
			t.Errorf("RunCompletion(%s) error: %v", shell, err)
		}
	}
	if err := RunCompletion(nil); err == nil {
		t.Error("RunCompletion(no args) = nil error, want usage error")
	}
	if err := RunCompletion([]string{"tcsh"}); err == nil {
		t.Error("RunCompletion(tcsh) = nil error, want error")
	}
}

func TestInstallCompletionBashAndFish(t *testing.T) {
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("XDG_DATA_HOME", filepath.Join(home, ".local", "share"))

	if err := installCompletion([]string{"bash"}); err != nil {
		t.Fatalf("installCompletion(bash): %v", err)
	}
	assertFile(t, filepath.Join(home, ".local", "share", "bash-completion", "completions", "cup"), "_cup")

	if err := installCompletion([]string{"fish"}); err != nil {
		t.Fatalf("installCompletion(fish): %v", err)
	}
	assertFile(t, filepath.Join(home, ".config", "fish", "completions", "cup.fish"), "complete -c cup")

	if err := installCompletion([]string{"tcsh"}); err == nil {
		t.Error("installCompletion(tcsh) = nil error, want error")
	}
}

func TestInstallCompletionZshWiresFpath(t *testing.T) {
	home := t.TempDir()
	t.Setenv("HOME", home)

	if err := installCompletion([]string{"zsh"}); err != nil {
		t.Fatalf("installCompletion(zsh): %v", err)
	}
	assertFile(t, filepath.Join(home, ".zsh", "completions", "_cup"), "compdef")
	assertFile(t, filepath.Join(home, ".zshrc"), "# cup completion")

	// A second install is idempotent: the .zshrc block is not duplicated.
	if err := installCompletion([]string{"zsh"}); err != nil {
		t.Fatalf("installCompletion(zsh) again: %v", err)
	}
	b, err := os.ReadFile(filepath.Join(home, ".zshrc"))
	if err != nil {
		t.Fatal(err)
	}
	if strings.Count(string(b), "# cup completion") != 1 {
		t.Errorf("zshrc marker written %d times, want 1", strings.Count(string(b), "# cup completion"))
	}
}
