package cmd

import (
	"fmt"
	"path/filepath"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/tmpl"
	"cup/internal/ui"
)

// This file holds the header-family (C++11/14/17) counterparts to the module
// scaffolding in add.go. A header lib gathers its components into a target whose
// primary header (<name>.hpp) is a thin aggregator that #includes each component
// — the header-world analogue of a module partition aggregator.
//
// A component kind is either header-only or compiled. A header-only kind (e.g.
// templated-class) is a single .hpp; its lib is an INTERFACE library that
// compiles nothing. A compiled kind (class, interface, enum, free-function) is a
// .h declaration paired with a .cpp definition; because INTERFACE libraries
// cannot compile sources, a lib holding any compiled component is a STATIC
// library — created STATIC when its first component is compiled, or promoted from
// INTERFACE when a compiled component is later added (see EnsureHeaderLibStatic).

// createHeaderLibAt scaffolds a new header lib target and registers it with its
// parent. Mirrors createLibAt for the headers family.
func createHeaderLibAt(proj *project.Project, name, targetDir, parentCmake string) error {
	kind, err := chooseKind(proj.Root, "headers")
	if err != nil {
		return err
	}
	symbol, err := ui.Text("primary symbol name?", scaffold.Capitalize(name), scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	namespace := scaffold.PathToNamespace(proj.Src(), targetDir)
	compiled := tmpl.IsCompiled(proj.Root, "headers", kind)
	ext := headerExt(compiled)
	primary := filepath.Join(targetDir, name+".hpp")
	cmake := filepath.Join(targetDir, cmakelists)

	if err := renderComponent(proj, kind, targetDir, symbol, namespace, compiled); err != nil {
		return err
	}
	if err := writeHeaderAggregator(proj, primary, symbol+ext); err != nil {
		return err
	}
	// The STATIC CMakeLists seeds its PRIVATE sources with {{symbol}}.cpp, so a
	// compiled kind needs symbol; the INTERFACE template simply ignores it.
	cml, err := scaffold.Render(proj.Root, "headers", kind, "CMakeLists.txt.tmpl",
		stdVars(proj, "name", name, "symbol", symbol))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, cmake, cml); err != nil {
		return err
	}
	if err := scaffold.AddHeaderSource(proj.Root, cmake, symbol+ext); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root, parentCmake, fmt.Sprintf("add_subdirectory(%s)", name))
}

// writeHeaderAggregator creates a lib's primary header as a thin aggregator that
// #includes one component header (a .h for a compiled component, a .hpp for a
// header-only one). A declined overwrite leaves the existing primary untouched.
// Mirrors writePrimaryAggregator.
func writeHeaderAggregator(proj *project.Project, primary, include string) error {
	wrote, err := scaffold.WriteFile(proj.Root, primary, "#pragma once\n")
	if err != nil || !wrote {
		return err
	}
	return scaffold.EnsureLine(proj.Root, primary, fmt.Sprintf("#include \"%s\"", include))
}

// addFileToHeaderLib adds another component to an existing header lib and wires it
// into the target and the primary aggregator. Mirrors addFileToLib.
func addFileToHeaderLib(proj *project.Project, libDir string) error {
	filename, err := ui.Text("new file name (no extension)?", "", scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	kind, err := chooseKind(proj.Root, "headers")
	if err != nil {
		return err
	}
	symbol, err := ui.Text("primary symbol name?", scaffold.Capitalize(filename), scaffold.ValidateIdent)
	if err != nil {
		return err
	}
	name := filepath.Base(libDir)
	namespace := scaffold.PathToNamespace(proj.Src(), libDir)
	primary := filepath.Join(libDir, name+".hpp")
	cmake := filepath.Join(libDir, cmakelists)
	compiled := tmpl.IsCompiled(proj.Root, "headers", kind)
	ext := headerExt(compiled)

	src, err := scaffold.Render(proj.Root, "headers", kind, sourceTmpl(compiled),
		stdVars(proj, "symbol", symbol, "namespace", namespace))
	if err != nil {
		return err
	}
	wrote, err := scaffold.WriteFile(proj.Root, filepath.Join(libDir, filename+ext), src)
	if err != nil || !wrote {
		return err
	}
	if compiled {
		if err := addCompiledDefinition(proj, kind, libDir, name, filename, symbol, namespace); err != nil {
			return err
		}
	}
	if err := scaffold.AddHeaderSource(proj.Root, cmake, filename+ext); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root, primary, fmt.Sprintf("#include \"%s\"", filename+ext))
}

// addCompiledDefinition wires a compiled component's definition into an existing
// lib: it promotes the lib to STATIC (a no-op if already so), writes the
// <filename>.cpp, and lists it among the lib's PRIVATE sources.
func addCompiledDefinition(proj *project.Project, kind, libDir, name, filename, symbol, namespace string) error {
	cmake := filepath.Join(libDir, cmakelists)
	if err := scaffold.EnsureHeaderLibStatic(proj.Root, cmake, name); err != nil {
		return err
	}
	cpp, err := scaffold.Render(proj.Root, "headers", kind, "source.cpp.tmpl",
		stdVars(proj, "symbol", symbol, "namespace", namespace, "header", filename+".h"))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, filepath.Join(libDir, filename+".cpp"), cpp); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root, cmake,
		fmt.Sprintf("target_sources(%s PRIVATE %s.cpp)", name, filename))
}

// renderComponent writes a lib's first component: for a compiled kind the
// <symbol>.h / <symbol>.cpp pair, for a header-only kind the single <symbol>.hpp.
func renderComponent(proj *project.Project, kind, targetDir, symbol, namespace string, compiled bool) error {
	src, err := scaffold.Render(proj.Root, "headers", kind, sourceTmpl(compiled),
		stdVars(proj, "symbol", symbol, "namespace", namespace))
	if err != nil {
		return err
	}
	if _, err := scaffold.WriteFile(proj.Root, filepath.Join(targetDir, symbol+headerExt(compiled)), src); err != nil {
		return err
	}
	if !compiled {
		return nil
	}
	cpp, err := scaffold.Render(proj.Root, "headers", kind, "source.cpp.tmpl",
		stdVars(proj, "symbol", symbol, "namespace", namespace, "header", symbol+".h"))
	if err != nil {
		return err
	}
	_, err = scaffold.WriteFile(proj.Root, filepath.Join(targetDir, symbol+".cpp"), cpp)
	return err
}

// headerExt is the component header extension: .h for a compiled component (paired
// with a .cpp), .hpp for a header-only one.
func headerExt(compiled bool) string {
	if compiled {
		return ".h"
	}
	return ".hpp"
}

// sourceTmpl names the header template for a component kind: the declaration
// header (source.h.tmpl) for a compiled kind, the whole header (source.hpp.tmpl)
// for a header-only one.
func sourceTmpl(compiled bool) string {
	if compiled {
		return "source.h.tmpl"
	}
	return "source.hpp.tmpl"
}
