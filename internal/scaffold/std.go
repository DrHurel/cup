package scaffold

import (
	"fmt"
	"strconv"
)

// Standards lists the C++ standards cup can scaffold, newest first — the order
// offered by `cup new` (the first is the default).
var Standards = []int{23, 20, 17, 14, 11}

// StdLabel renders a standard as it appears in the picker, e.g. 23 -> "c++23".
func StdLabel(std int) string { return "c++" + strconv.Itoa(std) }

// ParseStd reads a standard from a picker label ("c++23") or a bare number
// ("23"), rejecting anything cup does not scaffold.
func ParseStd(s string) (int, error) {
	if len(s) > 3 && s[:3] == "c++" {
		s = s[3:]
	}
	n, err := strconv.Atoi(s)
	if err == nil {
		for _, std := range Standards {
			if std == n {
				return n, nil
			}
		}
	}
	return 0, fmt.Errorf("unknown C++ standard %q", s)
}

// UsesModules reports whether std supports C++ modules (C++20+). Below that,
// cup scaffolds classic headers instead.
func UsesModules(std int) bool { return std >= 20 }

// Family maps a standard onto its template family: "modules" for C++20/23,
// "headers" for C++11/14/17. The family selects which embedded template subtree
// (files/<family>/…) cup renders from.
func Family(std int) string {
	if UsesModules(std) {
		return "modules"
	}
	return "headers"
}

// StdVars returns the per-standard template variables shared by the app, test,
// and library-component templates so no template hard-codes a standard:
//
//   - std_number  the bare standard, for `cxx_std_<n>` (e.g. "23")
//   - std_lib     standard-library access from a plain translation unit
//     (app/test): `import std;` on C++23, `#include <iostream>` below
//   - hello       the statement printing the app's greeting
//
// Module-interface (.cppm) templates additionally use std_prelude (a global
// module fragment carrying standard-library #includes, placed before the module
// declaration — set only on C++20) and std_import (`import std;`, placed after
// the declaration — set only on C++23).
func StdVars(std int) map[string]string {
	vars := map[string]string{
		"std_number": strconv.Itoa(std),
	}
	switch {
	case std >= 23:
		vars["std_lib"] = "import std;"
		vars["std_prelude"] = ""
		// Surrounding blank lines so the .cppm greeting matches C++23's original
		// spacing; empty std_prelude leaves nothing before the module decl.
		vars["std_import"] = "\nimport std;\n"
		vars["hello"] = `std::println("Hello from {{name}}!");`
	case std >= 20:
		vars["std_lib"] = "#include <iostream>"
		vars["std_prelude"] = "module;\n#include <iostream>\n"
		vars["std_import"] = ""
		vars["hello"] = `std::cout << "Hello from {{name}}!\n";`
	default:
		vars["std_lib"] = "#include <iostream>"
		vars["hello"] = `std::cout << "Hello from {{name}}!\n";`
	}
	return vars
}
