package cmd

import (
	"os"
	"path/filepath"
	"testing"
)

func TestIsDirIsFile(t *testing.T) {
	dir := t.TempDir()
	file := filepath.Join(dir, "f.txt")
	if err := os.WriteFile(file, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if !isDir(dir) {
		t.Error("isDir(dir) = false")
	}
	if isDir(file) {
		t.Error("isDir(file) = true")
	}
	if !isFile(file) {
		t.Error("isFile(file) = false")
	}
	if isFile(dir) {
		t.Error("isFile(dir) = true")
	}
	if isDir(filepath.Join(dir, "missing")) || isFile(filepath.Join(dir, "missing")) {
		t.Error("missing path reported as existing")
	}
}

func TestJoin(t *testing.T) {
	if got := join([]string{"a", "b", "c"}); got != "a b c" {
		t.Errorf("join = %q, want 'a b c'", got)
	}
	if got := join(nil); got != "" {
		t.Errorf("join(nil) = %q, want empty", got)
	}
}

func TestRunCommand(t *testing.T) {
	dir := t.TempDir()
	// A command that succeeds.
	if err := runCommand(dir, "true"); err != nil {
		t.Errorf("runCommand(true) = %v, want nil", err)
	}
	// A command that fails is wrapped as an error.
	if err := runCommand(dir, "false"); err == nil {
		t.Error("runCommand(false) = nil error, want error")
	}
}
