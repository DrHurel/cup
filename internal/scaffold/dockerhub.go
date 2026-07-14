package scaffold

import (
	"encoding/json"
	"fmt"
	"strings"
)

// dockerHubTagsURL is the Docker Hub registry endpoint listing a repository's
// tags, newest first. Overridable in tests so they never touch the network.
var dockerHubTagsURL = "https://hub.docker.com/v2/repositories/%s/tags/?page_size=100&ordering=last_updated"

// DockerHubTagsFunc is the source of a repository's tags; overridable in tests to
// return a fixed list without a fetch.
var DockerHubTagsFunc = fetchDockerHubTags

// DockerHubTags returns the tags of a Docker Hub repository, newest first, so
// `cup new` / `cup docker new` can offer a version to pick. A bare repo like
// "gcc" is resolved to the official "library/gcc"; a namespaced "org/repo" is
// used as-is.
func DockerHubTags(repo string) ([]string, error) { return DockerHubTagsFunc(repo) }

// normalizeRepo maps a bare repository name onto Docker Hub's "library"
// namespace (where official images live) and leaves an already-namespaced repo
// untouched.
func normalizeRepo(repo string) string {
	repo = strings.Trim(strings.TrimSpace(repo), "/")
	if !strings.Contains(repo, "/") {
		return "library/" + repo
	}
	return repo
}

func fetchDockerHubTags(repo string) ([]string, error) {
	body, err := httpGet(fmt.Sprintf(dockerHubTagsURL, normalizeRepo(repo)))
	if err != nil {
		return nil, err
	}
	return parseDockerHubTags(body), nil
}

// parseDockerHubTags pulls the tag names out of a Docker Hub tags response
// ({"results":[{"name":"14"},…]}), preserving the API's newest-first order and
// dropping blanks.
func parseDockerHubTags(body []byte) []string {
	var payload struct {
		Results []struct {
			Name string `json:"name"`
		} `json:"results"`
	}
	if err := json.Unmarshal(body, &payload); err != nil {
		return nil
	}
	tags := make([]string, 0, len(payload.Results))
	for _, r := range payload.Results {
		if r.Name != "" {
			tags = append(tags, r.Name)
		}
	}
	return tags
}
