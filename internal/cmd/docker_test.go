package cmd

import (
	"fmt"
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

// dockerBuild builds every image, hashing each Dockerfile to set its version,
// invoking `docker build` with the versioned and latest tags, and persisting the
// bumped version to cup.toml.
func TestDockerBuildBumpsVersionAndTags(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	t.Chdir(proj.Root)
	// The default image's Dockerfile is generated by the build (via sync).
	calls := stubRunCommand(t, nil)
	if err := dockerBuild(proj, nil); err != nil {
		t.Fatalf("dockerBuild: %v", err)
	}
	img, _ := proj.Config.Docker.Find("demo")
	if img.Version != 1 || img.Hash == "" {
		t.Fatalf("after first build: version=%d hash=%q, want version 1 and a hash", img.Version, img.Hash)
	}
	if len(*calls) != 1 || !strings.Contains((*calls)[0], "docker build -t demo:1 -t demo:latest") {
		t.Fatalf("docker call = %v", *calls)
	}
	// The bumped version is persisted to cup.toml.
	reloaded, err := project.Find()
	if err != nil {
		t.Fatalf("Find: %v", err)
	}
	if got, _ := reloaded.Config.Docker.Find("demo"); got.Version != 1 {
		t.Errorf("persisted version = %d, want 1", got.Version)
	}

	// A second build with unchanged content keeps the version.
	if err := dockerBuild(proj, nil); err != nil {
		t.Fatalf("dockerBuild (rebuild): %v", err)
	}
	if img, _ := proj.Config.Docker.Find("demo"); img.Version != 1 {
		t.Errorf("unchanged rebuild bumped version to %d, want 1", img.Version)
	}
}

// A failing `docker build` aborts dockerBuild and does not persist a version bump.
func TestDockerBuildDockerFails(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	t.Chdir(proj.Root)
	stubRunCommand(t, func(name string, args []string) error { return fmt.Errorf("docker boom") })
	if err := dockerBuild(proj, nil); err == nil {
		t.Error("dockerBuild(docker fails) = nil error, want error")
	}
	// The failed build must not have persisted a version bump: the on-disk config
	// still carries no built "demo" image (version 0, or absent entirely).
	reloaded, err := project.Find()
	if err != nil {
		t.Fatalf("Find: %v", err)
	}
	if got, ok := reloaded.Config.Docker.Find("demo"); ok && got.Version != 0 {
		t.Errorf("persisted version = %d after a failed build, want 0", got.Version)
	}
}

// dockerPush retags and pushes each built image's version and latest tags to the
// configured registry.
func TestDockerPushRetagsAndPushes(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	proj.Config.Docker.Registry = "docker.io/youruser"
	proj.Config.Docker.Images[0].Version = 2
	calls := stubRunCommand(t, nil)
	if err := dockerPush(proj, []string{"demo"}); err != nil {
		t.Fatalf("dockerPush: %v", err)
	}
	want := []string{
		"docker tag demo:2 docker.io/youruser/demo:2",
		"docker push docker.io/youruser/demo:2",
		"docker tag demo:latest docker.io/youruser/demo:latest",
		"docker push docker.io/youruser/demo:latest",
	}
	if strings.Join(*calls, "\n") != strings.Join(want, "\n") {
		t.Fatalf("docker calls =\n%s\nwant\n%s", strings.Join(*calls, "\n"), strings.Join(want, "\n"))
	}
}

// dockerPush prompts for the registry the first time and saves it to cup.toml.
func TestDockerPushPromptsAndSavesRegistry(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	t.Chdir(proj.Root)
	proj.Config.Docker.Images[0].Version = 1
	stubRunCommand(t, nil)
	feed(t, "docker.io/me\n")
	if err := dockerPush(proj, []string{"demo"}); err != nil {
		t.Fatalf("dockerPush: %v", err)
	}
	if proj.Config.Docker.Registry != "docker.io/me" {
		t.Errorf("registry = %q, want docker.io/me", proj.Config.Docker.Registry)
	}
	reloaded, err := project.Find()
	if err != nil {
		t.Fatalf("Find: %v", err)
	}
	if reloaded.Config.Docker.Registry != "docker.io/me" {
		t.Errorf("persisted registry = %q, want docker.io/me", reloaded.Config.Docker.Registry)
	}
}

// A failing `docker tag` surfaces the error out of pushImage.
func TestPushImageDockerFails(t *testing.T) {
	proj := newProjectWithImage(t, 23)
	stubRunCommand(t, func(name string, args []string) error { return fmt.Errorf("docker boom") })
	img := &proj.Config.Docker.Images[0]
	img.Version = 1
	if err := pushImage(proj, "docker.io/me", img); err == nil {
		t.Error("pushImage(docker fails) = nil error, want error")
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
