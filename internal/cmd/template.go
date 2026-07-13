package cmd

import (
	"fmt"
	"path/filepath"

	"cup/internal/project"
	"cup/internal/scaffold"
	"cup/internal/tmpl"
	"cup/internal/ui"
)

// RunTemplate handles `cup template <list|new>` — inspecting and adding
// project-local scaffolding templates under .cup/templates/.
func RunTemplate(args []string) error {
	sub := "list"
	if len(args) > 0 {
		sub = args[0]
		args = args[1:]
	}
	switch sub {
	case "list":
		return templateList()
	case "new":
		return templateNew(args)
	default:
		return fmt.Errorf("unknown template command %q (want: list, new)", sub)
	}
}

func templateList() error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	ui.Accent("library component kinds (used by `cup add lib`):")
	for _, kind := range tmpl.Kinds(proj.Root, family(proj)) {
		origin := "built-in"
		if isDir(proj.Path(tmpl.ProjectTemplateDir, kind)) {
			origin = "project"
		}
		fmt.Printf("  %-18s %s\n", kind, origin)
	}
	return nil
}

// templateNew copies a built-in template into .cup/templates/<name> so the
// project can edit it or use it as the base for a new kind.
func templateNew(args []string) error {
	proj, err := project.Find()
	if err != nil {
		return err
	}
	fam := family(proj)
	base, err := ui.Select("copy which built-in template as a starting point?", tmpl.BuiltinKinds(fam), "class")
	if err != nil {
		return err
	}
	def := base
	name, err := ui.Text("new template kind name?", def, scaffold.ValidateNonEmpty)
	if err != nil {
		return err
	}
	if len(args) > 0 {
		name = args[0]
	}
	dst := proj.Path(tmpl.ProjectTemplateDir, name)
	if isDir(dst) {
		ok, err := ui.Confirm(fmt.Sprintf("%s already exists. overwrite its files?", relTo(proj.Root, dst)), false)
		if err != nil {
			return err
		}
		if !ok {
			ui.Skipped(relTo(proj.Root, dst))
			return nil
		}
	}
	if err := tmpl.CopyBuiltin(fam, base, dst); err != nil {
		return err
	}
	ui.Wrote(filepath.Join(tmpl.ProjectTemplateDir, name) + "/")
	ui.Next(fmt.Sprintf("edit the files in %s, then use the kind in `cup add lib`", relTo(proj.Root, dst)))
	return nil
}
