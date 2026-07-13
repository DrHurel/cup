package scaffold

import (
	"strings"
	"testing"
)

func TestStdLabel(t *testing.T) {
	cases := map[int]string{
		23: "c++23",
		20: "c++20",
		11: "c++11",
	}
	for in, want := range cases {
		if got := StdLabel(in); got != want {
			t.Errorf("StdLabel(%d) = %q, want %q", in, got, want)
		}
	}
}

func TestParseStd(t *testing.T) {
	valid := map[string]int{
		"c++23": 23,
		"c++20": 20,
		"c++17": 17,
		"c++14": 14,
		"c++11": 11,
		"23":    23,
		"11":    11,
	}
	for in, want := range valid {
		got, err := ParseStd(in)
		if err != nil {
			t.Errorf("ParseStd(%q) returned error: %v", in, err)
			continue
		}
		if got != want {
			t.Errorf("ParseStd(%q) = %d, want %d", in, got, want)
		}
	}

	invalid := []string{"", "c++99", "99", "42", "c++", "abc", "c++ab", "2x"}
	for _, in := range invalid {
		if _, err := ParseStd(in); err == nil {
			t.Errorf("ParseStd(%q) = nil error, want error", in)
		}
	}
}

func TestParseStdErrorNamesInput(t *testing.T) {
	_, err := ParseStd("c++99")
	if err == nil {
		t.Fatal("ParseStd(c++99) = nil error, want error")
	}
	// The "c++" prefix is stripped before the value is quoted in the error.
	if !strings.Contains(err.Error(), `"99"`) {
		t.Errorf("error %q should quote the unknown standard 99", err)
	}
}

func TestUsesModules(t *testing.T) {
	for _, std := range []int{20, 23, 26} {
		if !UsesModules(std) {
			t.Errorf("UsesModules(%d) = false, want true", std)
		}
	}
	for _, std := range []int{11, 14, 17} {
		if UsesModules(std) {
			t.Errorf("UsesModules(%d) = true, want false", std)
		}
	}
}

func TestFamily(t *testing.T) {
	cases := map[int]string{
		23: "modules",
		20: "modules",
		17: "headers",
		14: "headers",
		11: "headers",
	}
	for std, want := range cases {
		if got := Family(std); got != want {
			t.Errorf("Family(%d) = %q, want %q", std, got, want)
		}
	}
}

func TestStdVars(t *testing.T) {
	// C++23: import std, no prelude, blank-line-wrapped std_import.
	v23 := StdVars(23)
	if v23["std_number"] != "23" {
		t.Errorf("StdVars(23) std_number = %q, want %q", v23["std_number"], "23")
	}
	if v23["std_lib"] != "import std;" {
		t.Errorf("StdVars(23) std_lib = %q, want %q", v23["std_lib"], "import std;")
	}
	if v23["std_prelude"] != "" {
		t.Errorf("StdVars(23) std_prelude = %q, want empty", v23["std_prelude"])
	}
	if v23["std_import"] != "\nimport std;\n" {
		t.Errorf("StdVars(23) std_import = %q, want %q", v23["std_import"], "\nimport std;\n")
	}
	if !strings.Contains(v23["hello"], "std::println") {
		t.Errorf("StdVars(23) hello = %q, want std::println greeting", v23["hello"])
	}

	// C++20: modules but no import std — uses a global-module-fragment prelude.
	v20 := StdVars(20)
	if v20["std_number"] != "20" {
		t.Errorf("StdVars(20) std_number = %q, want %q", v20["std_number"], "20")
	}
	if v20["std_lib"] != "#include <iostream>" {
		t.Errorf("StdVars(20) std_lib = %q, want include", v20["std_lib"])
	}
	if v20["std_prelude"] != "module;\n#include <iostream>\n" {
		t.Errorf("StdVars(20) std_prelude = %q, want module prelude", v20["std_prelude"])
	}
	if v20["std_import"] != "" {
		t.Errorf("StdVars(20) std_import = %q, want empty", v20["std_import"])
	}
	if !strings.Contains(v20["hello"], "std::cout") {
		t.Errorf("StdVars(20) hello = %q, want std::cout greeting", v20["hello"])
	}

	// C++17 (and below): classic headers, no module-only keys.
	v17 := StdVars(17)
	if v17["std_number"] != "17" {
		t.Errorf("StdVars(17) std_number = %q, want %q", v17["std_number"], "17")
	}
	if v17["std_lib"] != "#include <iostream>" {
		t.Errorf("StdVars(17) std_lib = %q, want include", v17["std_lib"])
	}
	if _, ok := v17["std_prelude"]; ok {
		t.Errorf("StdVars(17) should not set std_prelude")
	}
	if _, ok := v17["std_import"]; ok {
		t.Errorf("StdVars(17) should not set std_import")
	}
	if !strings.Contains(v17["hello"], "std::cout") {
		t.Errorf("StdVars(17) hello = %q, want std::cout greeting", v17["hello"])
	}
}
