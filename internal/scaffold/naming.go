package scaffold

import (
	"fmt"
	"path/filepath"
	"regexp"
	"strings"
)

var identRe = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_]*$`)

// ValidateIdent returns an error unless s is a legal C++ identifier.
func ValidateIdent(s string) error {
	if identRe.MatchString(s) {
		return nil
	}
	return fmt.Errorf("must be a valid C++ identifier")
}

// ValidateNonEmpty returns an error if s is blank.
func ValidateNonEmpty(s string) error {
	if strings.TrimSpace(s) == "" {
		return fmt.Errorf("must not be empty")
	}
	return nil
}

// Capitalize upper-cases the first rune, leaving the rest untouched — the default
// symbol name derived from a lib/file name (mylib -> Mylib).
func Capitalize(s string) string {
	if s == "" {
		return s
	}
	return strings.ToUpper(s[:1]) + s[1:]
}

// relParts returns the path segments of dir relative to src, dropping the leading
// top-level segment (apps / libs / tests). A lib at src/libs/utils yields
// ["utils"]; a nested src/libs/utils/json yields ["utils", "json"].
func relParts(src, dir string) []string {
	rel, err := filepath.Rel(src, dir)
	if err != nil {
		return nil
	}
	parts := strings.Split(filepath.ToSlash(rel), "/")
	if len(parts) <= 1 {
		return nil
	}
	return parts[1:]
}

// PathToNamespace derives a C++ namespace from a folder under src/, joining the
// path segments (below apps/libs/tests) with "::" and turning hyphens into
// underscores. src/libs/utils/json -> "utils::json".
func PathToNamespace(src, dir string) string {
	return joinParts(relParts(src, dir), "::")
}

// PathToModule mirrors PathToNamespace but joins with "." — the module-name
// separator. src/libs/utils/json -> "utils.json".
func PathToModule(src, dir string) string {
	return joinParts(relParts(src, dir), ".")
}

func joinParts(parts []string, sep string) string {
	out := make([]string, len(parts))
	for i, p := range parts {
		out[i] = strings.ReplaceAll(p, "-", "_")
	}
	return strings.Join(out, sep)
}
