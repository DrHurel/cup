// Package project locates and describes a cup project: the directory tree rooted
// at the nearest ancestor holding a cup.toml marker.
package project

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/BurntSushi/toml"
)

// Marker is the file whose presence identifies a cup project root.
const Marker = "cup.toml"

// Config is the parsed contents of cup.toml.
type Config struct {
	Name        string `toml:"name"`
	CupVersion  string `toml:"cup_version,omitempty"`
	CppStandard int    `toml:"cpp_standard,omitempty"`
}

// Standard returns the project's C++ standard, defaulting to 23 when unset so
// projects created before cpp_standard existed keep behaving as C++23.
func (c Config) Standard() int {
	if c.CppStandard == 0 {
		return 23
	}
	return c.CppStandard
}

// Project is a located cup project.
type Project struct {
	Root   string
	Config Config
}

// Src returns the project's src/ directory.
func (p *Project) Src() string { return filepath.Join(p.Root, "src") }

// UsesModules reports whether the project's standard supports C++ modules
// (C++20 and later); below that, cup scaffolds classic headers.
func (p *Project) UsesModules() bool { return p.Config.Standard() >= 20 }

// Path joins parts onto the project root.
func (p *Project) Path(parts ...string) string {
	return filepath.Join(append([]string{p.Root}, parts...)...)
}

// Find walks up from the current working directory looking for a cup.toml,
// returning the enclosing project. It errors if none is found.
func Find() (*Project, error) {
	cwd, err := os.Getwd()
	if err != nil {
		return nil, err
	}
	dir := cwd
	for {
		marker := filepath.Join(dir, Marker)
		if _, err := os.Stat(marker); err == nil {
			var cfg Config
			if _, err := toml.DecodeFile(marker, &cfg); err != nil {
				return nil, fmt.Errorf("reading %s: %w", marker, err)
			}
			return &Project{Root: dir, Config: cfg}, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return nil, fmt.Errorf("not inside a cup project (no %s found in this or any parent directory)", Marker)
		}
		dir = parent
	}
}

// WriteConfig writes cup.toml at root.
func WriteConfig(root string, cfg Config) error {
	f, err := os.Create(filepath.Join(root, Marker))
	if err != nil {
		return err
	}
	defer f.Close()
	return toml.NewEncoder(f).Encode(cfg)
}
