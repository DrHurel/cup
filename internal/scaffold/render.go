package scaffold

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"cup/internal/tmpl"
	"cup/internal/ui"
)

var placeholderRe = regexp.MustCompile(`\{\{[^}]+\}\}`)

// Render loads template <family>/<kind>/<name> (preferring the project's own
// copy under .cup/templates/<kind>/), substitutes every {{key}} from vars, and
// fails if any placeholder was left unresolved. Substitution runs to a fixed
// point, so a variable whose value itself contains a placeholder (e.g. a
// {{hello}} greeting embedding {{name}}) is fully resolved.
func Render(root, family, kind, name string, vars map[string]string) (string, error) {
	raw, err := tmpl.Read(root, family, kind, name)
	if err != nil {
		return "", fmt.Errorf("template %s/%s not found", kind, name)
	}
	content := string(raw)
	for i := 0; i <= len(vars); i++ {
		before := content
		for k, v := range vars {
			content = strings.ReplaceAll(content, "{{"+k+"}}", v)
		}
		if content == before {
			break
		}
	}
	if left := placeholderRe.FindAllString(content, -1); len(left) > 0 {
		set := map[string]bool{}
		for _, l := range left {
			set[l] = true
		}
		uniq := make([]string, 0, len(set))
		for l := range set {
			uniq = append(uniq, l)
		}
		sort.Strings(uniq)
		return "", fmt.Errorf("template %s/%s has unresolved placeholders: %s",
			kind, name, strings.Join(uniq, ", "))
	}
	return content, nil
}

// rel renders path relative to root for tidy log output.
func rel(root, path string) string {
	if r, err := filepath.Rel(root, path); err == nil {
		return r
	}
	return path
}

// WriteFile writes content to path, prompting before overwriting an existing
// file. It reports true if written, false if the user declined the overwrite —
// a legitimate skip, not an error.
func WriteFile(root, path, content string) (bool, error) {
	if _, err := os.Stat(path); err == nil {
		ok, err := ui.Confirm(fmt.Sprintf("%s exists. overwrite?", rel(root, path)), false)
		if err != nil {
			return false, err
		}
		if !ok {
			ui.Skipped(rel(root, path))
			return false, nil
		}
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return false, err
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		return false, err
	}
	ui.Wrote(rel(root, path))
	return true, nil
}

// EnsureFile creates path with content only if it does not already exist.
func EnsureFile(root, path, content string) error {
	if _, err := os.Stat(path); err == nil {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		return err
	}
	ui.Wrote(rel(root, path))
	return nil
}
