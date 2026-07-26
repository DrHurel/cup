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

// headerComponent is one component of a header lib — the identifiers needed to
// render its sources and wire them into the lib. Threaded through the helpers
// below in place of a long, easily-transposed parameter list.
type headerComponent struct {
	kind      string // component template kind (class, interface, enum, …)
	libDir    string // directory of the lib the component belongs to
	lib       string // the lib target's name
	filename  string // component file stem, without extension
	symbol    string // primary symbol the component declares
	namespace string
	compiled  bool // a .h/.cpp pair, rather than a single header-only .hpp
}

// header is the component's header file name (<filename>.h or <filename>.hpp).
func (c headerComponent) header() string { return c.filename + headerExt(c.compiled) }

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
	// A lib's first component is named after its primary symbol.
	c := headerComponent{
		kind:      kind,
		libDir:    targetDir,
		lib:       name,
		filename:  symbol,
		symbol:    symbol,
		namespace: scaffold.PathToNamespace(proj.Src(), targetDir),
		compiled:  tmpl.IsCompiled(proj.Root, "headers", kind),
	}
	primary := filepath.Join(targetDir, name+".hpp")
	cmake := filepath.Join(targetDir, cmakelists)

	if err := renderComponent(proj, c); err != nil {
		return err
	}
	if err := writeHeaderAggregator(proj, primary, c.header()); err != nil {
		return err
	}
	// Under Make the root Makefile finds this lib's .cpp sources by path and
	// archives them; no CMakeLists and no parent registration are written.
	if proj.UsesMake() {
		return nil
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
	if err := scaffold.AddHeaderSource(proj.Root, cmake, c.header()); err != nil {
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
	c := headerComponent{
		kind:      kind,
		libDir:    libDir,
		lib:       filepath.Base(libDir),
		filename:  filename,
		symbol:    symbol,
		namespace: scaffold.PathToNamespace(proj.Src(), libDir),
		compiled:  tmpl.IsCompiled(proj.Root, "headers", kind),
	}

	wrote, err := writeComponentHeader(proj, c)
	if err != nil || !wrote {
		return err
	}
	if err := wireHeaderComponent(proj, c); err != nil {
		return err
	}
	primary := filepath.Join(libDir, c.lib+".hpp")
	return scaffold.EnsureLine(proj.Root, primary, fmt.Sprintf("#include \"%s\"", c.header()))
}

// wireHeaderComponent registers a component whose header has just been written with
// the lib's build system. Under Make there is no per-lib CMakeLists — a compiled
// component's .cpp is written and then discovered by path — so the only shared file
// the caller touches is the lib's own aggregator header. Under CMake the .cpp is
// additionally listed among the lib's PRIVATE sources, alongside the header.
func wireHeaderComponent(proj *project.Project, c headerComponent) error {
	if proj.UsesMake() {
		if !c.compiled {
			return nil
		}
		return writeCompiledSource(proj, c)
	}
	if c.compiled {
		if err := addCompiledDefinition(proj, c); err != nil {
			return err
		}
	}
	return scaffold.AddHeaderSource(proj.Root, filepath.Join(c.libDir, cmakelists), c.header())
}

// addCompiledDefinition wires a compiled component's definition into an existing
// lib: it promotes the lib to STATIC (a no-op if already so), writes the
// <filename>.cpp, and lists it among the lib's PRIVATE sources.
func addCompiledDefinition(proj *project.Project, c headerComponent) error {
	cmake := filepath.Join(c.libDir, cmakelists)
	if err := scaffold.EnsureHeaderLibStatic(proj.Root, cmake, c.lib); err != nil {
		return err
	}
	if err := writeCompiledSource(proj, c); err != nil {
		return err
	}
	return scaffold.EnsureLine(proj.Root, cmake,
		fmt.Sprintf("target_sources(%s PRIVATE %s.cpp)", c.lib, c.filename))
}

// writeCompiledSource renders and writes a compiled component's <filename>.cpp
// definition (its .h counterpart is written by writeComponentHeader).
// Build-system-agnostic — shared by the CMake wiring in addCompiledDefinition and
// the Make path.
func writeCompiledSource(proj *project.Project, c headerComponent) error {
	cpp, err := scaffold.Render(proj.Root, "headers", c.kind, "source.cpp.tmpl",
		stdVars(proj, "symbol", c.symbol, "namespace", c.namespace, "header", c.filename+".h"))
	if err != nil {
		return err
	}
	_, err = scaffold.WriteFile(proj.Root, filepath.Join(c.libDir, c.filename+".cpp"), cpp)
	return err
}

// writeComponentHeader renders and writes a component's header: the declaration
// header (<filename>.h) for a compiled kind, the whole header (<filename>.hpp) for
// a header-only one. Reports whether the file was written — a declined overwrite
// reports false with a nil error.
func writeComponentHeader(proj *project.Project, c headerComponent) (bool, error) {
	src, err := scaffold.Render(proj.Root, "headers", c.kind, sourceTmpl(c.compiled),
		stdVars(proj, "symbol", c.symbol, "namespace", c.namespace))
	if err != nil {
		return false, err
	}
	return scaffold.WriteFile(proj.Root, filepath.Join(c.libDir, c.header()), src)
}

// renderComponent writes a lib's first component: for a compiled kind the
// <symbol>.h / <symbol>.cpp pair, for a header-only kind the single <symbol>.hpp.
func renderComponent(proj *project.Project, c headerComponent) error {
	if _, err := writeComponentHeader(proj, c); err != nil {
		return err
	}
	if !c.compiled {
		return nil
	}
	return writeCompiledSource(proj, c)
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
