#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TempDir.hpp"

import cup.cmd;
import cup.project;
import cup.platform;
import cup.ui;

namespace {

using cup::cmd::Dependency;
using cup::project::Config;
using cup::project::Project;
using cup::test::TempDir;

class ScopedStdin {
public:
    explicit ScopedStdin(std::string_view keys) {
        int fds[2]{};
        REQUIRE(::pipe(fds) == 0);
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        REQUIRE(::write(fds[1], keys.data(), keys.size()) == static_cast<ssize_t>(keys.size()));
        REQUIRE(::close(fds[1]) == 0);
        REQUIRE(::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO);
        REQUIRE(::close(fds[0]) == 0);
    }
    ScopedStdin(const ScopedStdin&) = delete;
    ScopedStdin& operator=(const ScopedStdin&) = delete;
    ~ScopedStdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
    }

private:
    int saved_ = -1;
};

template <typename T>
class ScopedOverride {
public:
    ScopedOverride(T& slot, T value) : slot_(slot), previous_(slot) { slot_ = std::move(value); }
    ~ScopedOverride() { slot_ = std::move(previous_); }
    ScopedOverride(const ScopedOverride&) = delete;
    ScopedOverride& operator=(const ScopedOverride&) = delete;

private:
    T& slot_;
    T previous_;
};

class ScopedCwd {
public:
    explicit ScopedCwd(const std::filesystem::path& dir)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~ScopedCwd() { std::filesystem::current_path(previous_); }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;

private:
    std::filesystem::path previous_;
};

class StubRunCommand {
public:
    explicit StubRunCommand(bool fail = false)
        : fail_(fail),
          override_(cup::platform::run_command_func(),
                    cup::platform::RunCommandFunc{
                        [this](const std::filesystem::path&, std::string_view name,
                              std::span<const std::string> args)
                            -> std::expected<void, cup::error::Error> {
                            std::string line(name);
                            for (const auto& arg : args) {
                                line += ' ';
                                line += arg;
                            }
                            calls_.push_back(std::move(line));
                            if (fail_) {
                                return std::unexpected(cup::error::Error("stubbed failure"));
                            }
                            return {};
                        }}) {}

    [[nodiscard]] const std::vector<std::string>& calls() const { return calls_; }

private:
    bool fail_;
    std::vector<std::string> calls_;
    ScopedOverride<cup::platform::RunCommandFunc> override_;
};

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool file_contains(const std::filesystem::path& path, std::string_view needle) {
    const auto content = read_whole_file(path);
    return content.has_value() && content->find(needle) != std::string::npos;
}

Project make_project(const TempDir& dir, std::string_view tool = "cmake") {
    Config cfg{.name = "demo", .cpp_standard = 20, .build_tool = std::string(tool)};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    // A root CMakeLists so prepare_third_party's ensure_line_before(src/libs)
    // has an anchor to insert ahead of, mirroring a real scaffolded project.
    dir.write("CMakeLists.txt", "project(demo VERSION 0.1.0 LANGUAGES C CXX)\n\nadd_subdirectory(src/libs)\n");
    return Project{dir.path(), cfg};
}

}

TEST_CASE("discover_dependencies (cmake) parses submodule, download and apt registrations",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    dir.write("third_party/CMakeLists.txt",
              "include(FetchContent)\n"
              "add_subdirectory(fmt)\n"
              "FetchContent_Declare(\n  json\n  GIT_REPOSITORY x\n  GIT_TAG y\n)\n"
              "FetchContent_MakeAvailable(json)\n"
              "find_package(Boost REQUIRED) # cup-apt: libboost-dev\n");

    const auto deps = cup::cmd::discover_dependencies(proj);
    REQUIRE(deps.size() == 3);
    REQUIRE(deps[0] == Dependency{.name = "fmt", .method = "git-submodule"});
    REQUIRE(deps[1] == Dependency{.name = "json", .method = "cmake-download"});
    REQUIRE(deps[2] == Dependency{.name = "Boost", .method = "apt-install"});
}

TEST_CASE("discover_dependencies with no third-party file is empty, not an error",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    REQUIRE(cup::cmd::discover_dependencies(proj).empty());
}

TEST_CASE("register_submodule adds the git submodule and registers it", "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("fmt\nhttps://example.com/fmt.git\nv10\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::register_submodule(proj).has_value());
    REQUIRE(stub.calls().size() == 1);
    REQUIRE(stub.calls()[0] ==
            "git submodule add --branch v10 https://example.com/fmt.git third_party/fmt");
    REQUIRE(file_contains(dir.path() / "third_party" / "CMakeLists.txt", "add_subdirectory(fmt)"));
    REQUIRE(file_contains(dir.path() / "CMakeLists.txt", "add_subdirectory(third_party)"));
}

TEST_CASE("register_submodule with a blank ref omits --branch", "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("fmt\nhttps://example.com/fmt.git\n\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::register_submodule(proj).has_value());
    REQUIRE(stub.calls()[0] == "git submodule add https://example.com/fmt.git third_party/fmt");
}

TEST_CASE("register_submodule surfaces a failing git command", "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    StubRunCommand stub(/*fail=*/true);

    const ScopedStdin not_a_terminal("");
    std::istringstream in("fmt\nhttps://example.com/fmt.git\n\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE_FALSE(cup::cmd::register_submodule(proj).has_value());
}

TEST_CASE("register_download appends a FetchContent block", "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);

    const ScopedStdin not_a_terminal("");
    std::istringstream in("json\nhttps://example.com/json.git\nv3\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::register_download(proj).has_value());
    const auto content = read_whole_file(dir.path() / "third_party" / "CMakeLists.txt");
    REQUIRE(content.has_value());
    REQUIRE(content->find("FetchContent_Declare(") != std::string::npos);
    REQUIRE(content->find("GIT_REPOSITORY https://example.com/json.git") != std::string::npos);
    REQUIRE(content->find("FetchContent_MakeAvailable(json)") != std::string::npos);
}

TEST_CASE("register_apt (cmake) records a find_package line and offers to install",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("Boost\nlibboost-dev\ny\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::register_apt(proj).has_value());
    REQUIRE(stub.calls() == std::vector<std::string>{"sudo apt-get install -y libboost-dev"});
    REQUIRE(file_contains(dir.path() / "third_party" / "CMakeLists.txt",
                          "find_package(Boost REQUIRED) # cup-apt: libboost-dev"));
}

TEST_CASE("register_apt declining the install skips the apt-get call", "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("Boost\nlibboost-dev\nn\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::register_apt(proj).has_value());
    REQUIRE(stub.calls().empty());
}

TEST_CASE("register_apt (make) tags third_party.mk instead of a find_package line",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir, "make");
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("libboost-dev\nn\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::register_apt(proj).has_value());
    REQUIRE(file_contains(dir.path() / "third_party" / "third_party.mk", "# cup-apt: libboost-dev"));
}

TEST_CASE("run_register dispatches on the chosen fetch method", "[cmd][thirdparty]") {
    const TempDir dir;
    make_project(dir);
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    // method=submodule(1), then register_submodule's own 3 prompts.
    std::istringstream in("1\nfmt\nhttps://example.com/fmt.git\n\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::run_register({}).has_value());
    REQUIRE(file_contains(dir.path() / "third_party" / "CMakeLists.txt", "add_subdirectory(fmt)"));
}

TEST_CASE("run_register outside a project reports project::find's error", "[cmd][thirdparty]") {
    const TempDir dir;
    const ScopedCwd cwd(dir.path());
    REQUIRE_FALSE(cup::cmd::run_register({}).has_value());
}

TEST_CASE("resolve_dependency", "[cmd][thirdparty]") {
    const std::vector<Dependency> deps{{"fmt", "git-submodule"}, {"json", "cmake-download"}};

    SECTION("an explicit name that exists") {
        auto got = cup::cmd::resolve_dependency(deps, std::vector<std::string>{"json"});
        REQUIRE(got.has_value());
        REQUIRE(*got == deps[1]);
    }
    SECTION("an explicit name that does not exist") {
        REQUIRE_FALSE(cup::cmd::resolve_dependency(deps, std::vector<std::string>{"bogus"}).has_value());
    }
    SECTION("no name prompts interactively") {
        const ScopedStdin not_a_terminal("");
        std::istringstream in("2\n");
        const cup::ui::ScopedInput scoped(in);
        auto got = cup::cmd::resolve_dependency(deps, {});
        REQUIRE(got.has_value());
        REQUIRE(*got == deps[1]);
    }
}

TEST_CASE("remove_submodule deinits, rm -f's, and drops the CMakeLists line",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    dir.write("third_party/CMakeLists.txt", "add_subdirectory(fmt)\n");
    StubRunCommand stub;

    REQUIRE(cup::cmd::remove_submodule(proj, "fmt").has_value());
    REQUIRE(stub.calls() == std::vector<std::string>{"git submodule deinit -f third_party/fmt",
                                                      "git rm -f third_party/fmt"});
    REQUIRE_FALSE(file_contains(dir.path() / "third_party" / "CMakeLists.txt", "add_subdirectory(fmt)"));
}

TEST_CASE("remove_download removes the FetchContent block, erroring if none is found",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);

    SECTION("no matching block") {
        dir.write("third_party/CMakeLists.txt", "include(FetchContent)\n");
        REQUIRE_FALSE(cup::cmd::remove_download(proj, "json").has_value());
    }
    SECTION("a real block") {
        dir.write("third_party/CMakeLists.txt",
                  "FetchContent_Declare(\n  json\n  GIT_REPOSITORY x\n  GIT_TAG y\n)\n"
                  "FetchContent_MakeAvailable(json)\n");
        REQUIRE(cup::cmd::remove_download(proj, "json").has_value());
        REQUIRE_FALSE(
            file_contains(dir.path() / "third_party" / "CMakeLists.txt", "FetchContent_Declare"));
    }
}

TEST_CASE("remove_apt removes the find_package line and syncs the default build image",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    dir.write("third_party/CMakeLists.txt", "find_package(Boost REQUIRED) # cup-apt: libboost-dev\n");

    REQUIRE(cup::cmd::remove_apt(proj, "Boost").has_value());
    REQUIRE_FALSE(file_contains(dir.path() / "third_party" / "CMakeLists.txt", "find_package(Boost"));
}

TEST_CASE("remove_apt reports when nothing matches", "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    dir.write("third_party/CMakeLists.txt", "include(FetchContent)\n");
    REQUIRE_FALSE(cup::cmd::remove_apt(proj, "Boost").has_value());
}

// A dependency name containing regex metacharacters (apt package names are
// not restricted to identifier syntax) must still match itself literally,
// not as a pattern — the C++ analogue of Go's regexp.QuoteMeta guard.
TEST_CASE("remove_apt escapes regex metacharacters in the dependency name",
          "[cmd][thirdparty]") {
    const TempDir dir;
    const Project proj = make_project(dir, "make");
    dir.write("third_party/third_party.mk", "# cup-apt: python3.11-dev\n");

    REQUIRE(cup::cmd::remove_apt(proj, "python3.11-dev").has_value());
    REQUIRE_FALSE(
        file_contains(dir.path() / "third_party" / "third_party.mk", "python3.11-dev"));
}

TEST_CASE("run_unregister with nothing registered is a no-op success", "[cmd][thirdparty]") {
    const TempDir dir;
    make_project(dir);
    const ScopedCwd cwd(dir.path());
    REQUIRE(cup::cmd::run_unregister({}).has_value());
}

TEST_CASE("run_unregister declining the confirm leaves the registration in place",
          "[cmd][thirdparty]") {
    const TempDir dir;
    make_project(dir);
    dir.write("third_party/CMakeLists.txt", "add_subdirectory(fmt)\n");
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    // resolve_dependency always prompts (even with a single dependency, like
    // its Go original), so this needs an answer too before the confirm's.
    std::istringstream in("1\nn\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::run_unregister({}).has_value());
    REQUIRE(stub.calls().empty());
    REQUIRE(file_contains(dir.path() / "third_party" / "CMakeLists.txt", "add_subdirectory(fmt)"));
}

TEST_CASE("run_unregister dispatches to remove_submodule on confirm", "[cmd][thirdparty]") {
    const TempDir dir;
    make_project(dir);
    dir.write("third_party/CMakeLists.txt", "add_subdirectory(fmt)\n");
    const ScopedCwd cwd(dir.path());
    StubRunCommand stub;

    const ScopedStdin not_a_terminal("");
    std::istringstream in("y\n");
    const cup::ui::ScopedInput scoped(in);

    REQUIRE(cup::cmd::run_unregister(std::vector<std::string>{"fmt"}).has_value());
    REQUIRE_FALSE(file_contains(dir.path() / "third_party" / "CMakeLists.txt", "add_subdirectory(fmt)"));
}

TEST_CASE("run_unregister outside a project reports project::find's error", "[cmd][thirdparty]") {
    const TempDir dir;
    const ScopedCwd cwd(dir.path());
    REQUIRE_FALSE(cup::cmd::run_unregister({}).has_value());
}
