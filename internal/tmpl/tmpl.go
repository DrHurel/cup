// Package tmpl holds the built-in scaffolding templates, embedded into the cup
// binary, and resolves them against per-project overrides.
//
// A template "kind" (class, interface, app, test, …) is a directory of
// {{placeholder}} files. cup ships a default set here; a project may add its own
// kinds — or override a built-in — by dropping a directory of the same shape into
// .cup/templates/<kind>/ at its root. Resolution always prefers the project copy.
package tmpl

import (
	"embed"
	"os"
	"path"
	"path/filepath"
	"sort"
)

//go:embed all:files
var embedded embed.FS

// ProjectTemplateDir is the per-project directory that holds template overrides
// and additions, relative to the project root.
const ProjectTemplateDir = ".cup/templates"

// hasComponentSource reports whether directory <kind> carries the source file(s)
// that mark it a usable library-component kind for family. In the headers family
// a kind is either header-only (source.hpp.tmpl) or compiled — a declaration /
// definition pair (source.h.tmpl + source.cpp.tmpl). Module kinds carry a single
// interface unit (source.cppm.tmpl).
func hasComponentSource(root, family, kind string) bool {
	if family == "headers" {
		return Exists(root, family, kind, "source.hpp.tmpl") || IsCompiled(root, family, kind)
	}
	return Exists(root, family, kind, "source.cppm.tmpl")
}

// IsCompiled reports whether a headers-family kind scaffolds a compiled component
// — a .h declaration paired with a .cpp definition (source.h.tmpl +
// source.cpp.tmpl) — rather than a single header-only .hpp. Templates (e.g.
// templated-class) stay header-only, so they are not compiled. Module kinds are
// never compiled in this sense.
func IsCompiled(root, family, kind string) bool {
	return family == "headers" &&
		Exists(root, family, kind, "source.h.tmpl") &&
		Exists(root, family, kind, "source.cpp.tmpl")
}

// Read returns the bytes of template file <kind>/<name>, preferring a
// project-local copy under <root>/.cup/templates/<kind>/ over the built-in
// default in files/<family>/<kind>/. root may be empty to consult only the
// built-in templates.
func Read(root, family, kind, name string) ([]byte, error) {
	if root != "" {
		local := filepath.Join(root, ProjectTemplateDir, kind, name)
		if b, err := os.ReadFile(local); err == nil {
			return b, nil
		}
	}
	return embedded.ReadFile(path.Join("files", family, kind, name))
}

// Exists reports whether template file <kind>/<name> is available for family,
// either as a project-local override or a built-in default.
func Exists(root, family, kind, name string) bool {
	if root != "" {
		if _, err := os.Stat(filepath.Join(root, ProjectTemplateDir, kind, name)); err == nil {
			return true
		}
	}
	_, err := embedded.Open(path.Join("files", family, kind, name))
	return err == nil
}

// Kinds lists the library-component template kinds available to a project for
// the given family: the union of built-in kinds and project-local ones, minus
// the special app / test / project directories. A kind qualifies only if it
// carries the family's component source file.
func Kinds(root, family string) []string {
	seen := map[string]bool{}
	add := func(name string) {
		switch name {
		case "app", "test", "project":
			return
		}
		if hasComponentSource(root, family, name) {
			seen[name] = true
		}
	}
	entries, _ := embedded.ReadDir(path.Join("files", family))
	for _, e := range entries {
		if e.IsDir() {
			add(e.Name())
		}
	}
	if root != "" {
		local, _ := os.ReadDir(filepath.Join(root, ProjectTemplateDir))
		for _, e := range local {
			if e.IsDir() {
				add(e.Name())
			}
		}
	}
	kinds := make([]string, 0, len(seen))
	for k := range seen {
		kinds = append(kinds, k)
	}
	sort.Strings(kinds)
	return kinds
}

// BuiltinKinds lists every built-in template directory for family (including
// app, test, and project), used by `cup template new` to offer a starting point
// to copy.
func BuiltinKinds(family string) []string {
	var kinds []string
	entries, _ := embedded.ReadDir(path.Join("files", family))
	for _, e := range entries {
		if e.IsDir() {
			kinds = append(kinds, e.Name())
		}
	}
	sort.Strings(kinds)
	return kinds
}

// CopyBuiltin writes every file of a built-in template <family>/<kind> into dst,
// so a project can start from a copy of a default and edit it.
func CopyBuiltin(family, kind, dst string) error {
	if err := os.MkdirAll(dst, 0o755); err != nil {
		return err
	}
	entries, err := embedded.ReadDir(path.Join("files", family, kind))
	if err != nil {
		return err
	}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		b, err := embedded.ReadFile(path.Join("files", family, kind, e.Name()))
		if err != nil {
			return err
		}
		if err := os.WriteFile(filepath.Join(dst, e.Name()), b, 0o644); err != nil {
			return err
		}
	}
	return nil
}
