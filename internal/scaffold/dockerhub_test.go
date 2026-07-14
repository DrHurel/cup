package scaffold

import (
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
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

// fetchDockerHubTags builds the request URL from dockerHubTagsURL (normalizing the
// repo into the library namespace) and parses the response, all against a local
// test server so it never touches the network.
func TestFetchDockerHubTags(t *testing.T) {
	var gotPath string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.Path
		w.Write([]byte(`{"results":[{"name":"14"},{"name":"13"}]}`))
	}))
	defer srv.Close()

	prev := dockerHubTagsURL
	dockerHubTagsURL = srv.URL + "/%s/tags"
	defer func() { dockerHubTagsURL = prev }()

	got, err := fetchDockerHubTags("gcc")
	if err != nil {
		t.Fatalf("fetchDockerHubTags: %v", err)
	}
	if !reflect.DeepEqual(got, []string{"14", "13"}) {
		t.Errorf("fetchDockerHubTags = %v, want [14 13]", got)
	}
	// The bare repo was normalized into the library namespace before the request.
	if !strings.Contains(gotPath, "library/gcc") {
		t.Errorf("request path = %q, want it to contain library/gcc", gotPath)
	}
}

// A non-200 response surfaces as an error rather than empty tags.
func TestFetchDockerHubTagsHTTPError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "nope", http.StatusNotFound)
	}))
	defer srv.Close()

	prev := dockerHubTagsURL
	dockerHubTagsURL = srv.URL + "/%s/tags"
	defer func() { dockerHubTagsURL = prev }()

	if _, err := fetchDockerHubTags("gcc"); err == nil {
		t.Error("fetchDockerHubTags(404) = nil error, want error")
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
