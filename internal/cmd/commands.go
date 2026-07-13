package cmd

// Command is one cup subcommand: its name, one-line summary, and entry point.
type Command struct {
	Name    string
	Summary string
	Run     func(args []string) error
}

// Commands is the canonical list of cup subcommands, shared by main (dispatch +
// usage) and the shell-completion generator so the two never drift. It is built
// in init() rather than as a var literal so the completion command may reference
// it (RunCompletion reads Commands) without a static initialization cycle.
var Commands []Command

func init() {
	Commands = []Command{
		{"new", "create a new C++ project", RunNew},
		{"add", "scaffold an app, lib, or test (interactive)", RunAdd},
		{"configure", "generate the CMake build system [MODE]", RunConfigure},
		{"build", "configure + compile [MODE]", RunBuild},
		{"rebuild", "wipe build/ then compile [MODE]", RunRebuild},
		{"run", "build then run an app [MODE] [app] [-- args]", RunRun},
		{"test", "build then run the test suite [MODE]", RunTest},
		{"retest", "wipe build/ then run the test suite [MODE]", RunRetest},
		{"clean", "remove the build/ directory", RunClean},
		{"register", "register a third-party dependency", RunRegister},
		{"unregister", "remove a third-party dependency [name]", RunUnregister},
		{"template", "list or add project-local templates <list|new>", RunTemplate},
		{"completion", "install or print shell completion <install|bash|zsh|fish>", RunCompletion},
	}
}
