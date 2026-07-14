package scaffold

import (
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"
)

func TestFirstNonZero(t *testing.T) {
	if got := firstNonZero(7, 3); got != 7 {
		t.Errorf("firstNonZero(7, 3) = %d, want 7", got)
	}
	if got := firstNonZero(0, 3); got != 3 {
		t.Errorf("firstNonZero(0, 3) = %d, want 3", got)
	}
}

func TestNewestCompilersFunc(t *testing.T) {
	orig := NewestCompilersFunc
	t.Cleanup(func() { NewestCompilersFunc = orig })
	NewestCompilersFunc = func() (int, int) { return 42, 99 }
	if gcc, clang := NewestCompilers(); gcc != 42 || clang != 99 {
		t.Errorf("NewestCompilers() = (%d, %d), want (42, 99)", gcc, clang)
	}
}

func TestHTTPGet(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/missing" {
			w.WriteHeader(http.StatusNotFound)
			return
		}
		_, _ = w.Write([]byte("hello"))
	}))
	t.Cleanup(srv.Close)

	body, err := httpGet(srv.URL + "/ok")
	if err != nil {
		t.Fatalf("httpGet ok: %v", err)
	}
	if string(body) != "hello" {
		t.Errorf("httpGet body = %q, want hello", body)
	}

	if _, err := httpGet(srv.URL + "/missing"); err == nil {
		t.Error("httpGet on 404 = nil error, want error")
	}
	if _, err := httpGet("://not-a-url"); err == nil {
		t.Error("httpGet on a malformed url = nil error, want error")
	}
}

func TestReleaseCacheRoundTrip(t *testing.T) {
	// os.UserCacheDir honours XDG_CACHE_HOME on Linux, so point it at a temp dir to
	// keep the test off the real cache.
	t.Setenv("XDG_CACHE_HOME", t.TempDir())

	if _, ok := readReleaseCache(); ok {
		t.Error("readReleaseCache on an empty dir = ok, want miss")
	}

	want := releaseCache{GCC: 15, Clang: 20, FetchedAt: time.Now().Truncate(time.Second)}
	if err := writeReleaseCache(want); err != nil {
		t.Fatalf("writeReleaseCache: %v", err)
	}
	path, err := releaseCachePath()
	if err != nil {
		t.Fatalf("releaseCachePath: %v", err)
	}
	if filepath.Base(path) != "compiler-releases.json" {
		t.Errorf("cache path = %q, want it to end in compiler-releases.json", path)
	}

	got, ok := readReleaseCache()
	if !ok {
		t.Fatal("readReleaseCache after write = miss, want hit")
	}
	if got.GCC != 15 || got.Clang != 20 {
		t.Errorf("cached = (%d, %d), want (15, 20)", got.GCC, got.Clang)
	}
}

// withReleaseServers points the fetchers at test servers returning the given
// bodies, restoring the live URLs afterwards.
func withReleaseServers(t *testing.T, gccBody, clangBody string, status int) {
	t.Helper()
	handler := func(body string) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if status != http.StatusOK {
				w.WriteHeader(status)
				return
			}
			_, _ = w.Write([]byte(body))
		}
	}
	gccSrv := httptest.NewServer(handler(gccBody))
	clangSrv := httptest.NewServer(handler(clangBody))
	t.Cleanup(gccSrv.Close)
	t.Cleanup(clangSrv.Close)

	origGCC, origClang := gccReleasesURL, clangReleasesURL
	gccReleasesURL, clangReleasesURL = gccSrv.URL, clangSrv.URL
	t.Cleanup(func() { gccReleasesURL, clangReleasesURL = origGCC, origClang })
}

func TestFetchNewestCompilersFromNetwork(t *testing.T) {
	t.Setenv("XDG_CACHE_HOME", t.TempDir()) // no cache -> forces the network path
	withReleaseServers(t,
		`<a href="gcc-15.1.0/">gcc-15.1.0/</a>`,
		`[{"tag_name":"llvmorg-20.1.8","prerelease":false}]`,
		http.StatusOK)

	gcc, clang := fetchNewestCompilers()
	if gcc != 15 || clang != 20 {
		t.Errorf("fetchNewestCompilers() = (%d, %d), want (15, 20)", gcc, clang)
	}
	// The successful fetch should have populated the cache.
	if c, ok := readReleaseCache(); !ok || c.GCC != 15 || c.Clang != 20 {
		t.Errorf("cache after fetch = %+v (ok=%v), want gcc=15 clang=20", c, ok)
	}
}

func TestFetchNewestCompilersFallback(t *testing.T) {
	t.Setenv("XDG_CACHE_HOME", t.TempDir())
	// Both endpoints error, and there is no cached value, so the bundled fallbacks win.
	withReleaseServers(t, "", "", http.StatusInternalServerError)

	gcc, clang := fetchNewestCompilers()
	if gcc != gccNewestFallback || clang != clangNewestFallback {
		t.Errorf("fetchNewestCompilers() offline = (%d, %d), want fallbacks (%d, %d)",
			gcc, clang, gccNewestFallback, clangNewestFallback)
	}
}

func TestFetchNewestCompilersServesFreshCache(t *testing.T) {
	t.Setenv("XDG_CACHE_HOME", t.TempDir())
	if err := writeReleaseCache(releaseCache{GCC: 13, Clang: 18, FetchedAt: time.Now()}); err != nil {
		t.Fatalf("seed cache: %v", err)
	}
	// Servers would return other numbers; a fresh cache must short-circuit before them.
	withReleaseServers(t,
		`<a href="gcc-99.1.0/">gcc-99.1.0/</a>`,
		`[{"tag_name":"llvmorg-99.1.0","prerelease":false}]`,
		http.StatusOK)

	if gcc, clang := fetchNewestCompilers(); gcc != 13 || clang != 18 {
		t.Errorf("fetchNewestCompilers() with fresh cache = (%d, %d), want cached (13, 18)", gcc, clang)
	}
}

func TestFetchGCCAndClangNewest(t *testing.T) {
	withReleaseServers(t,
		`<a href="gcc-14.2.0/">gcc-14.2.0/</a>`,
		`[{"tag_name":"llvmorg-19.1.0","prerelease":false}]`,
		http.StatusOK)
	if got := fetchGCCNewest(); got != 14 {
		t.Errorf("fetchGCCNewest() = %d, want 14", got)
	}
	if got := fetchClangNewest(); got != 19 {
		t.Errorf("fetchClangNewest() = %d, want 19", got)
	}

	// On a transport error both return 0 so the caller can fall back.
	withReleaseServers(t, "", "", http.StatusInternalServerError)
	if got := fetchGCCNewest(); got != 0 {
		t.Errorf("fetchGCCNewest() on error = %d, want 0", got)
	}
	if got := fetchClangNewest(); got != 0 {
		t.Errorf("fetchClangNewest() on error = %d, want 0", got)
	}
}
