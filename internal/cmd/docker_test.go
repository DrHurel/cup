package cmd

import (
	"os"
	"strings"
	"testing"

	"cup/internal/project"
	"cup/internal/scaffold"
)

// withStubTags points scaffold.DockerHubTags at a fixed list for the duration of a
// test so chooseBaseImage never touches the network.
func withStubTags(t *testing.T, tags []string, err error) {
	t.Helper()
	prev := scaffold.DockerHubTagsFunc
	scaffold.DockerHubTagsFunc = func(string) ([]string, error) { return tags, err }
	t.Cleanup(func() { scaffold.DockerHubTagsFunc = prev })
}

func TestRenderBuildDockerfile(t *testing.T) {
	// No packages -> a bare FROM, no apt layer.
	bare := renderBuildDockerfile("gcc:14", nil)
	if !strings.Contains(bare, "FROM gcc:14") || strings.Contains(bare, "apt-get") {
		t.Errorf("bare Dockerfile unexpected:\n%s", bare)
	}
	// With packages -> a single install layer listing them.
	withPkgs := renderBuildDockerfile("debian:trixie-slim", []string{"libboost-dev", "libfmt-dev"})
	for _, want := range []string{"FROM debian:trixie-slim", "libboost-dev", "libfmt-dev", "rm -rf /var/lib/apt/lists/*"} {
		if !strings.Contains(withPkgs, want) {
			t.Errorf("Dockerfile missing %q:\n%s", want, withPkgs)
		}
	}
}

func TestNextVersion(t *testing.T) {
	// First build (no prior hash) -> 1.
	if got := nextVersion(0, "", "h1"); got != 1 {
		t.Errorf("nextVersion(first) = %d, want 1", got)
	}
	// Unchanged content of an already-built image -> unchanged version.
	if got := nextVersion(3, "h1", "h1"); got != 3 {
		t.Errorf("nextVersion(unchanged) = %d, want 3", got)
	}
	// Changed content -> bump.
	if got := nextVersion(3, "h1", "h2"); got != 4 {
		t.Errorf("nextVersion(changed) = %d, want 4", got)
	}
}

func TestChooseBaseImageFromTags(t *testing.T) {
	withStubTags(t, []string{"14", "13", "12"}, nil)
	// repo prompt, then tag Select (option 1 = "14", newest-first).
	feed(t, "gcc\n1\n")
	got, err := chooseBaseImage()
	if err != nil {
		t.Fatalf("chooseBaseImage: %v", err)
	}
	if got != "gcc:14" {
		t.Errorf("chooseBaseImage = %q, want gcc:14", got)
	}
}

func TestChooseBaseImageFallsBackWhenTagsUnavailable(t *testing.T) {
	withStubTags(t, nil, os.ErrDeadlineExceeded)
	// repo prompt, then a free-text tag because the fetch failed.
	feed(t, "myorg/base\nbookworm\n")
	got, err := chooseBaseImage()
	if err != nil {
		t.Fatalf("chooseBaseImage: %v", err)
	}
	if got != "myorg/base:bookworm" {
		t.Errorf("chooseBaseImage = %q, want myorg/base:bookworm", got)
	}
}

func TestDockerNewScaffolds(t *testing.T) {
	proj := newProject(t, 20)
	t.Chdir(proj.Root)
	withStubTags(t, []string{"trixie-slim"}, nil)
	// image name, repo, tag choice (numbered menu, option 1).
	feed(t, "runtime\ndebian\n1\n")
	if err := dockerNew(proj); err != nil {
		t.Fatalf("dockerNew: %v", err)
	}
	assertFile(t, dockerfilePath(proj, "runtime"), "FROM debian:trixie-slim")

	img, ok := proj.Config.Docker.Find("runtime")
	if !ok || img.Base != "debian:trixie-slim" || img.Default {
		t.Fatalf("runtime image entry = %+v, %v", img, ok)
	}

	// A second image with the same name is refused.
	feed(t, "runtime\ndebian\n")
	if err := dockerNew(proj); err == nil {
		t.Error("dockerNew(duplicate name) = nil error, want error")
	}
}

func TestSyncDefaultBuildImageNoDefault(t *testing.T) {
	// A project without a default image is a no-op (no file, no error).
	proj := newProject(t, 20)
	if err := syncDefaultBuildImage(proj); err != nil {
		t.Fatalf("syncDefaultBuildImage: %v", err)
	}
	if isFile(dockerfilePath(proj, "demo")) {
		t.Error("syncDefaultBuildImage wrote a Dockerfile for a project with no default image")
	}
}

func TestImageTagAndHashFile(t *testing.T) {
	if got := imageTag("myproj", 3); got != "myproj:3" {
		t.Errorf("imageTag = %q, want myproj:3", got)
	}

	dir := t.TempDir()
	path := dir + "/Dockerfile"
	if err := os.WriteFile(path, []byte("FROM gcc:14\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	h1, err := hashFile(path)
	if err != nil || len(h1) != 64 {
		t.Fatalf("hashFile = %q, %v; want a 64-char hex digest", h1, err)
	}
	// Same content hashes the same; a missing file errors.
	if h2, _ := hashFile(path); h2 != h1 {
		t.Errorf("hashFile not deterministic: %q vs %q", h1, h2)
	}
	if _, err := hashFile(dir + "/missing"); err == nil {
		t.Error("hashFile(missing) = nil error, want error")
	}
}

// RunDocker dispatches on the subcommand and rejects a missing/unknown one, all
// without touching docker.
func TestRunDockerDispatch(t *testing.T) {
	proj := newProject(t, 20)
	t.Chdir(proj.Root) // RunDocker resolves the project from the working dir
	if err := RunDocker(nil); err == nil {
		t.Error("RunDocker(nil) = nil error, want usage error")
	}
	if err := RunDocker([]string{"bogus"}); err == nil {
		t.Error("RunDocker(bogus) = nil error, want error")
	}
	// build/push against a project with no images fail before reaching docker.
	if err := RunDocker([]string{"build"}); err == nil {
		t.Error("RunDocker(build, no images) = nil error, want error")
	}
	if err := RunDocker([]string{"push"}); err == nil {
		t.Error("RunDocker(push, no images) = nil error, want error")
	}
}

// buildImage fails cleanly (before invoking docker) when a non-default image has
// no Dockerfile on disk.
func TestBuildImageMissingDockerfile(t *testing.T) {
	proj := newProject(t, 20)
	img := &project.DockerImage{Name: "runtime", Base: "debian:trixie-slim"}
	if err := buildImage(proj, img); err == nil {
		t.Error("buildImage(no Dockerfile) = nil error, want error")
	}
}

// dockerPush refuses an image that has never been built (version 0) before it
// would shell out to docker.
func TestDockerPushUnbuilt(t *testing.T) {
	proj := newProject(t, 20)
	proj.Config.Docker.Registry = "docker.io/youruser" // skip the prompt
	proj.Config.Docker.Images = []project.DockerImage{{Name: "demo", Base: "gcc:14", Default: true}}
	if err := dockerPush(proj, []string{"demo"}); err == nil {
		t.Error("dockerPush(unbuilt) = nil error, want error")
	}
}

func TestSelectImagesErrors(t *testing.T) {
	proj := newProject(t, 20)
	// No images defined at all.
	if _, err := selectImages(proj, nil); err == nil {
		t.Error("selectImages(no images) = nil error, want error")
	}
	proj.Config.Docker.Images = []project.DockerImage{{Name: "demo", Base: "gcc:14", Default: true}}
	// Unknown name.
	if _, err := selectImages(proj, []string{"ghost"}); err == nil {
		t.Error("selectImages(unknown) = nil error, want error")
	}
	// No arg -> all images.
	all, err := selectImages(proj, nil)
	if err != nil || len(all) != 1 || all[0].Name != "demo" {
		t.Fatalf("selectImages(all) = %+v, %v", all, err)
	}
}
