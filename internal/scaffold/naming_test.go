package scaffold

import (
	"path/filepath"
	"testing"
)

func TestValidateIdent(t *testing.T) {
	valid := []string{"x", "Foo", "_hidden", "a1", "MyClass", "snake_case", "__", "A0_9z"}
	for _, s := range valid {
		if err := ValidateIdent(s); err != nil {
			t.Errorf("ValidateIdent(%q) = %v, want nil", s, err)
		}
	}

	invalid := []string{"", "1abc", "has space", "has-hyphen", "dot.dot", "ns::name", "é"}
	for _, s := range invalid {
		if err := ValidateIdent(s); err == nil {
			t.Errorf("ValidateIdent(%q) = nil, want error", s)
		}
	}
}

func TestValidateNonEmpty(t *testing.T) {
	if err := ValidateNonEmpty("ok"); err != nil {
		t.Errorf("ValidateNonEmpty(%q) = %v, want nil", "ok", err)
	}
	for _, s := range []string{"", " ", "\t", "\n  \t"} {
		if err := ValidateNonEmpty(s); err == nil {
			t.Errorf("ValidateNonEmpty(%q) = nil, want error", s)
		}
	}
}

func TestCapitalize(t *testing.T) {
	cases := map[string]string{
		"":         "",
		"mylib":    "Mylib",
		"Mylib":    "Mylib",
		"a":        "A",
		"aBC":      "ABC",
		"_leading": "_leading",
	}
	for in, want := range cases {
		if got := Capitalize(in); got != want {
			t.Errorf("Capitalize(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestPathToNamespace(t *testing.T) {
	src := filepath.FromSlash("/proj/src")
	cases := map[string]string{
		"/proj/src/libs":            "",      // top-level only, no sub-parts
		"/proj/src/libs/utils":      "utils", // single segment
		"/proj/src/libs/utils/json": "utils::json",
		"/proj/src/libs/my-lib":     "my_lib", // hyphen -> underscore
		"/proj/src/apps/cli/tools":  "cli::tools",
	}
	for dir, want := range cases {
		if got := PathToNamespace(src, filepath.FromSlash(dir)); got != want {
			t.Errorf("PathToNamespace(%q, %q) = %q, want %q", src, dir, got, want)
		}
	}
}

func TestPathToModule(t *testing.T) {
	src := filepath.FromSlash("/proj/src")
	cases := map[string]string{
		"/proj/src/libs/utils":      "utils",
		"/proj/src/libs/utils/json": "utils.json",
		"/proj/src/libs/my-lib/net": "my_lib.net",
	}
	for dir, want := range cases {
		if got := PathToModule(src, filepath.FromSlash(dir)); got != want {
			t.Errorf("PathToModule(%q, %q) = %q, want %q", src, dir, got, want)
		}
	}
}
