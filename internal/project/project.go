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
	Name        string         `toml:"name"`
	CupVersion  string         `toml:"cup_version,omitempty"`
	CppStandard int            `toml:"cpp_standard,omitempty"`
	Compiler    CompilerConfig `toml:"compiler,omitempty"`
}

// CompilerConfig is the `[compiler]` table in cup.toml: the minimum compiler
// major versions the project's generated CMakeLists enforces, plus the docker
// image `cup compiler` compiles in to verify a version change before committing
// it. GCC and Clang are pinned independently — a project may enforce one, both,
// or neither. An unpinned compiler is nil and is left out of cup.toml entirely
// (rather than written as a meaningless `gcc = 0`).
type CompilerConfig struct {
	GCC         *int   `toml:"gcc,omitempty"`
	Clang       *int   `toml:"clang,omitempty"`
	VerifyImage string `toml:"verify_image,omitempty"`
}

// NewCompilerConfig builds a [compiler] table from major versions, treating 0 as
// "no floor" — that compiler is left unpinned (nil) and omitted from cup.toml.
func NewCompilerConfig(gcc, clang int) CompilerConfig {
	return CompilerConfig{GCC: floorPtr(gcc), Clang: floorPtr(clang)}
}

func floorPtr(v int) *int {
	if v <= 0 {
		return nil
	}
	return &v
}

// GCCFloor and ClangFloor return the pinned major version, or 0 when the compiler
// is unpinned, so callers can work in plain ints (0 = no floor).
func (c CompilerConfig) GCCFloor() int   { return deref(c.GCC) }
func (c CompilerConfig) ClangFloor() int { return deref(c.Clang) }

func deref(p *int) int {
	if p == nil {
		return 0
	}
	return *p
}

// HasFloor reports whether cup.toml pins any compiler minimum. When it does not
// (older projects predate the [compiler] table), callers fall back to cup's
// per-standard defaults.
func (c CompilerConfig) HasFloor() bool { return c.GCC != nil || c.Clang != nil }

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
