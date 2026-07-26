// TempDir — the C++ counterpart of Go's t.TempDir(), used by the ported suites.
#pragma once

#include <unistd.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>

namespace cup::test {

// TempDir creates a unique directory and removes it, with everything under it,
// when it goes out of scope.
//
// The name carries the process id so concurrent ctest runs cannot collide, and a
// counter so several dirs in one test cannot. Unlike Go's t.TempDir the path is
// not canonicalised — nothing here re-derives it from the working directory, so a
// symlinked /tmp cannot cause the mismatch canonicalTempDir() guards against in
// the Go suite.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                std::format("cup-test-{}-{}", ::getpid(), counter++);
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);  // best effort; a test failure must not throw here
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    operator const std::filesystem::path&() const { return path_; }  // NOLINT(*-explicit-*)

    // write creates <dir>/relative, making parent directories as needed.
    void write(const std::filesystem::path& relative, std::string_view content) const {
        const std::filesystem::path target = path_ / relative;
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out << content;
    }

private:
    std::filesystem::path path_;
};

}  // namespace cup::test
