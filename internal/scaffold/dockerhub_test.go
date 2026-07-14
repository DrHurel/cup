package scaffold

import (
	"reflect"
	"testing"
)

func TestParseDockerHubTags(t *testing.T) {
	body := []byte(`{"count":3,"results":[{"name":"14"},{"name":"13-bookworm"},{"name":""},{"name":"latest"}]}`)
	got := parseDockerHubTags(body)
	want := []string{"14", "13-bookworm", "latest"} // order preserved, blank dropped
	if !reflect.DeepEqual(got, want) {
		t.Errorf("parseDockerHubTags = %v, want %v", got, want)
	}

	// Malformed JSON yields no tags rather than panicking.
	if got := parseDockerHubTags([]byte("not json")); got != nil {
		t.Errorf("parseDockerHubTags(bad) = %v, want nil", got)
	}
}

func TestDockerHubTagsUsesFunc(t *testing.T) {
	prev := DockerHubTagsFunc
	DockerHubTagsFunc = func(repo string) ([]string, error) { return []string{"stub:" + repo}, nil }
	defer func() { DockerHubTagsFunc = prev }()

	got, err := DockerHubTags("gcc")
	if err != nil || len(got) != 1 || got[0] != "stub:gcc" {
		t.Fatalf("DockerHubTags = %v, %v; want [stub:gcc], nil", got, err)
	}
}

func TestNormalizeRepo(t *testing.T) {
	cases := map[string]string{
		"gcc":           "library/gcc",
		"debian":        "library/debian",
		"silkeh/clang":  "silkeh/clang",
		"  ubuntu  ":    "library/ubuntu",
		"/library/gcc/": "library/gcc",
	}
	for in, want := range cases {
		if got := normalizeRepo(in); got != want {
			t.Errorf("normalizeRepo(%q) = %q, want %q", in, got, want)
		}
	}
}
