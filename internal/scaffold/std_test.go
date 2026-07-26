package scaffold

import (
	"strconv"
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
	cases := []struct {
		name string
		std  int
		// stdModule is what the project asked for; below C++23 there is no std module
		// to grant, so asking cannot produce one.
		stdModule bool
		lib       string
		hello     string
		// modules is whether the module-only keys exist at all: the headers family
		// must not set them, even empty.
		modules   bool
		prelude   string
		importStd string
	}{
		{
			name: "c++23 on the std module", std: 23, stdModule: true,
			lib: "import std;", hello: "std::println",
			// Blank lines around the import so the .cppm greeting keeps its spacing;
			// an empty prelude leaves nothing before the module declaration.
			modules: true, prelude: "", importStd: "\nimport std;\n",
		},
		{
			// GCC 14: std::println comes from the standard <print>, in a global module
			// fragment, and never from an `import std;`.
			name: "c++23 without the std module", std: 23, stdModule: false,
			lib: "#include <print>", hello: "std::println",
			modules: true, prelude: "module;\n#include <print>\n", importStd: "",
		},
		{
			name: "c++20 cannot have the std module", std: 20, stdModule: true,
			lib: "#include <iostream>", hello: "std::cout",
			modules: true, prelude: "module;\n#include <iostream>\n", importStd: "",
		},
		{
			name: "c++17 headers", std: 17, stdModule: false,
			lib: "#include <iostream>", hello: "std::cout",
			modules: false,
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			v := StdVars(c.std, c.stdModule)
			if want := strconv.Itoa(c.std); v["std_number"] != want {
				t.Errorf("std_number = %q, want %q", v["std_number"], want)
			}
			if v["std_lib"] != c.lib {
				t.Errorf("std_lib = %q, want %q", v["std_lib"], c.lib)
			}
			if !strings.Contains(v["hello"], c.hello) {
				t.Errorf("hello = %q, want a %s greeting", v["hello"], c.hello)
			}
			assertModuleKeys(t, v, c.modules, c.prelude, c.importStd)
		})
	}
}

// assertModuleKeys checks the .cppm-only pair: present with the given values for the
// modules family, absent entirely below C++20.
func assertModuleKeys(t *testing.T, v map[string]string, modules bool, prelude, importStd string) {
	t.Helper()
	gotPrelude, hasPrelude := v["std_prelude"]
	gotImport, hasImport := v["std_import"]
	if !modules {
		if hasPrelude || hasImport {
			t.Errorf("headers family set module-only keys: std_prelude=%q std_import=%q", gotPrelude, gotImport)
		}
		return
	}
	if !hasPrelude || !hasImport {
		t.Fatalf("modules family missing a module-only key: std_prelude set=%v std_import set=%v", hasPrelude, hasImport)
	}
	if gotPrelude != prelude {
		t.Errorf("std_prelude = %q, want %q", gotPrelude, prelude)
	}
	if gotImport != importStd {
		t.Errorf("std_import = %q, want %q", gotImport, importStd)
	}
}
