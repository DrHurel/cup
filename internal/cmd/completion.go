package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"cup/internal/ui"
)

// modeCommands are the subcommands that take a leading build MODE argument.
var modeCommands = []string{"configure", "build", "rebuild", "run", "test", "retest"}

// subcommandNames returns every cup subcommand name, from the single source of
// truth so completion never drifts from dispatch.
func subcommandNames() []string {
	names := make([]string, len(Commands))
	for i, c := range Commands {
		names[i] = c.Name
	}
	return names
}

// RunCompletion prints a shell completion script for the requested shell to
// stdout, or, given "install", wires it into the shell's startup so completion
// works automatically with no manual sourcing. Completions are generated from
// Commands, buildModes, and categories so they stay in sync with the CLI.
func RunCompletion(args []string) error {
	shell := ""
	if len(args) > 0 {
		shell = args[0]
	}
	if shell == "install" {
		return installCompletion(args[1:])
	}
	switch shell {
	case "bash":
		fmt.Print(bashCompletion())
	case "zsh":
		fmt.Print(zshCompletion())
	case "fish":
		fmt.Print(fishCompletion())
	default:
		return fmt.Errorf("usage: cup completion <bash|zsh|fish|install [shell]>")
	}
	return nil
}

// scriptFor returns the completion script for a supported shell.
func scriptFor(shell string) (string, error) {
	switch shell {
	case "bash":
		return bashCompletion(), nil
	case "zsh":
		return zshCompletion(), nil
	case "fish":
		return fishCompletion(), nil
	default:
		return "", fmt.Errorf("unsupported shell %q (want bash, zsh, or fish)", shell)
	}
}

// detectShell guesses the current shell from $SHELL, defaulting to bash.
func detectShell() string {
	switch filepath.Base(os.Getenv("SHELL")) {
	case "zsh":
		return "zsh"
	case "fish":
		return "fish"
	default:
		return "bash"
	}
}

// installCompletion writes the completion script to the shell's auto-loaded
// location so completion works in new shells without any manual sourcing. The
// shell may be given explicitly; otherwise it is detected from $SHELL.
func installCompletion(args []string) error {
	shell := ""
	if len(args) > 0 {
		shell = args[0]
	}
	if shell == "" {
		shell = detectShell()
	}
	script, err := scriptFor(shell)
	if err != nil {
		return err
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return err
	}

	// dest is a file each shell auto-loads on startup: the per-user completion
	// directory for bash and fish, and an fpath directory we register for zsh.
	var dest string
	switch shell {
	case "bash":
		dir := filepath.Join(dataHome(home), "bash-completion", "completions")
		dest = filepath.Join(dir, "cup")
	case "fish":
		dir := filepath.Join(home, ".config", "fish", "completions")
		dest = filepath.Join(dir, "cup.fish")
	case "zsh":
		dir := filepath.Join(home, ".zsh", "completions")
		dest = filepath.Join(dir, "_cup")
		if err := ensureZshFpath(home, dir); err != nil {
			return err
		}
	}

	if err := os.MkdirAll(filepath.Dir(dest), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(dest, []byte(script), 0o644); err != nil {
		return err
	}
	ui.Wrote(dest)
	ui.Success("cup " + shell + " completion installed — open a new shell to use it.")
	return nil
}

// dataHome returns $XDG_DATA_HOME or its ~/.local/share default.
func dataHome(home string) string {
	if x := os.Getenv("XDG_DATA_HOME"); x != "" {
		return x
	}
	return filepath.Join(home, ".local", "share")
}

// ensureZshFpath makes sure ~/.zshrc puts dir on fpath and runs compinit, so the
// installed _cup function is picked up automatically. It appends an idempotent,
// marker-guarded block only when one is not already present.
func ensureZshFpath(home, dir string) error {
	zshrc := filepath.Join(home, ".zshrc")
	const marker = "# cup completion"
	if existing, err := os.ReadFile(zshrc); err == nil {
		if strings.Contains(string(existing), marker) {
			return nil
		}
	} else if !os.IsNotExist(err) {
		return err
	}
	block := fmt.Sprintf("\n%s\nfpath=(%q $fpath)\nautoload -Uz compinit && compinit\n", marker, dir)
	f, err := os.OpenFile(zshrc, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	if _, err := f.WriteString(block); err != nil {
		return err
	}
	ui.Updated(zshrc)
	return nil
}

func bashCompletion() string {
	return fmt.Sprintf(`# cup bash completion.
# Install it automatically with:  cup completion install
# Or load it for one shell with:  source <(cup completion bash)
_cup() {
    local cur cmd
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [ "$COMP_CWORD" -eq 1 ]; then
        COMPREPLY=( $(compgen -W "%s" -- "$cur") )
        return
    fi
    cmd="${COMP_WORDS[1]}"
    case "$cmd" in
        add) COMPREPLY=( $(compgen -W "%s" -- "$cur") );;
        %s) COMPREPLY=( $(compgen -W "%s" -- "$cur") );;
        template) COMPREPLY=( $(compgen -W "list new" -- "$cur") );;
        completion) COMPREPLY=( $(compgen -W "bash zsh fish install" -- "$cur") );;
    esac
}
complete -F _cup cup
`,
		strings.Join(subcommandNames(), " "),
		strings.Join(categories, " "),
		strings.Join(modeCommands, "|"),
		strings.Join(buildModes, " "),
	)
}

func zshCompletion() string {
	return fmt.Sprintf(`#compdef cup
# cup zsh completion.
# Install it automatically with:  cup completion install
# Or load it for one shell with:  source <(cup completion zsh)
_cup() {
    if (( CURRENT == 2 )); then
        compadd -- %s
        return
    fi
    case ${words[2]} in
        add) compadd -- %s;;
        %s) compadd -- %s;;
        template) compadd -- list new;;
        completion) compadd -- bash zsh fish install;;
    esac
}
compdef _cup cup
`,
		strings.Join(subcommandNames(), " "),
		strings.Join(categories, " "),
		strings.Join(modeCommands, "|"),
		strings.Join(buildModes, " "),
	)
}

func fishCompletion() string {
	var b strings.Builder
	b.WriteString("# cup fish completion.\n")
	b.WriteString("# Install it automatically with:  cup completion install\n")
	b.WriteString("complete -c cup -f\n")
	for _, c := range Commands {
		fmt.Fprintf(&b, "complete -c cup -n __fish_use_subcommand -a %s -d %s\n",
			c.Name, fishQuote(c.Summary))
	}
	fmt.Fprintf(&b, "complete -c cup -n '__fish_seen_subcommand_from add' -a '%s'\n",
		strings.Join(categories, " "))
	fmt.Fprintf(&b, "complete -c cup -n '__fish_seen_subcommand_from %s' -a '%s'\n",
		strings.Join(modeCommands, " "), strings.Join(buildModes, " "))
	b.WriteString("complete -c cup -n '__fish_seen_subcommand_from template' -a 'list new'\n")
	b.WriteString("complete -c cup -n '__fish_seen_subcommand_from completion' -a 'bash zsh fish install'\n")
	return b.String()
}

// fishQuote wraps s in single quotes, escaping any single quotes within.
func fishQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", `\'`) + "'"
}
