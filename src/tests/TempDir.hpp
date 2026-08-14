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
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    operator const std::filesystem::path&() const { return path_; }

    void write(const std::filesystem::path& relative, std::string_view content) const {
        const std::filesystem::path target = path_ / relative;
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out << content;
    }

private:
    std::filesystem::path path_;
};

}
