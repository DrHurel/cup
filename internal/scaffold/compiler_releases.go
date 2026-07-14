package scaffold

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"sync"
	"time"

	"cup/internal/ui"
)

// gccNewestFallback and clangNewestFallback are the newest majors cup assumes
// when the live release lists cannot be reached (offline, rate-limited, or a
// changed upstream format). The baseline (MinCompilers) always wins as the floor,
// so a stale fallback only ever narrows the top of the picker, never breaks it.
const (
	gccNewestFallback   = 15
	clangNewestFallback = 20
)

// releaseCacheTTL bounds how long a fetched release list is trusted before cup
// looks again. Compiler majors ship a few times a year at most, so a week keeps
// `cup new` off the network on all but the occasional run.
const releaseCacheTTL = 7 * 24 * time.Hour

// Overridable in tests so they never touch the network; each points at the live
// source in normal use.
var (
	gccReleasesURL   = "https://ftp.gnu.org/gnu/gcc/"
	clangReleasesURL = "https://api.github.com/repos/llvm/llvm-project/releases?per_page=10"
	releaseHTTP      = &http.Client{Timeout: 4 * time.Second}

	// NewestCompilersFunc is the source of the picker's ceiling; overridable in
	// tests to return fixed versions without a fetch.
	NewestCompilersFunc = fetchNewestCompilers
)

// NewestCompilers returns the newest released GCC and Clang major versions, used
// as the ceiling of the `cup new` floor picker.
func NewestCompilers() (gcc, clang int) { return NewestCompilersFunc() }

type releaseCache struct {
	GCC       int       `json:"gcc"`
	Clang     int       `json:"clang"`
	FetchedAt time.Time `json:"fetched_at"`
}

// fetchNewestCompilers resolves the ceiling from, in order of preference: a fresh
// on-disk cache, a live fetch (cached on success), the last cached value, and
// finally the bundled fallback constants — so it always returns usable versions
// and only reaches the network about once a week.
func fetchNewestCompilers() (gcc, clang int) {
	if c, ok := readReleaseCache(); ok && time.Since(c.FetchedAt) < releaseCacheTTL {
		return c.GCC, c.Clang
	}
	ui.Running("checking latest gcc/clang releases")

	var wg sync.WaitGroup
	wg.Add(2)
	go func() { defer wg.Done(); gcc = fetchGCCNewest() }()
	go func() { defer wg.Done(); clang = fetchClangNewest() }()
	wg.Wait()

	cached, _ := readReleaseCache()
	if gcc == 0 {
		gcc = firstNonZero(cached.GCC, gccNewestFallback)
	}
	if clang == 0 {
		clang = firstNonZero(cached.Clang, clangNewestFallback)
	}
	_ = writeReleaseCache(releaseCache{GCC: gcc, Clang: clang, FetchedAt: time.Now()})
	return gcc, clang
}

var gccDirRe = regexp.MustCompile(`gcc-(\d+)\.\d+`)

// fetchGCCNewest reads the GNU gcc release index and returns the newest major,
// or 0 if it cannot be determined.
func fetchGCCNewest() int {
	body, err := httpGet(gccReleasesURL)
	if err != nil {
		return 0
	}
	return parseGCCNewest(body)
}

// parseGCCNewest returns the largest gcc major named in a directory listing like
// the GNU FTP index (entries such as "gcc-15.1.0/").
func parseGCCNewest(body []byte) int {
	newest := 0
	for _, m := range gccDirRe.FindAllStringSubmatch(string(body), -1) {
		if v, err := strconv.Atoi(m[1]); err == nil && v > newest {
			newest = v
		}
	}
	return newest
}

var clangTagRe = regexp.MustCompile(`llvmorg-(\d+)\.`)

// fetchClangNewest reads the LLVM GitHub releases and returns the newest stable
// major, or 0 if it cannot be determined.
func fetchClangNewest() int {
	body, err := httpGet(clangReleasesURL)
	if err != nil {
		return 0
	}
	return parseClangNewest(body)
}

// parseClangNewest returns the largest major among non-prerelease LLVM releases,
// whose tags look like "llvmorg-20.1.8".
func parseClangNewest(body []byte) int {
	var rels []struct {
		TagName    string `json:"tag_name"`
		Prerelease bool   `json:"prerelease"`
	}
	if err := json.Unmarshal(body, &rels); err != nil {
		return 0
	}
	newest := 0
	for _, r := range rels {
		if r.Prerelease {
			continue
		}
		m := clangTagRe.FindStringSubmatch(r.TagName)
		if m == nil {
			continue
		}
		if v, err := strconv.Atoi(m[1]); err == nil && v > newest {
			newest = v
		}
	}
	return newest
}

// httpGet fetches url with cup's timeout and a User-Agent (GitHub rejects
// requests without one), capping the body so a surprising response can't exhaust
// memory.
func httpGet(url string) ([]byte, error) {
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "cup")
	req.Header.Set("Accept", "application/vnd.github+json")
	resp, err := releaseHTTP.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %s: %s", url, resp.Status)
	}
	return io.ReadAll(io.LimitReader(resp.Body, 8<<20))
}

func releaseCachePath() (string, error) {
	dir, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "cup", "compiler-releases.json"), nil
}

func readReleaseCache() (releaseCache, bool) {
	var c releaseCache
	path, err := releaseCachePath()
	if err != nil {
		return c, false
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return c, false
	}
	if err := json.Unmarshal(b, &c); err != nil {
		return c, false
	}
	return c, true
}

func writeReleaseCache(c releaseCache) error {
	path, err := releaseCachePath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	b, err := json.Marshal(c)
	if err != nil {
		return err
	}
	return os.WriteFile(path, b, 0o644)
}

func firstNonZero(a, b int) int {
	if a != 0 {
		return a
	}
	return b
}
