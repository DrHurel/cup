#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "TempDir.hpp"

import cup.cmd;
import cup.project;
import cup.tmpl;
import cup.ui;

namespace {

using cup::project::Config;
using cup::project::Project;
using cup::test::TempDir;

// Points the real stdin fd at a pipe holding keys, so cup.ui's is_tty check
// sees "not a terminal". Mirrors ui_test.cpp's ScopedStdin.
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

// Sets an environment variable for the test's lifetime, restoring (or
// unsetting) it afterward. Mirrors releases_test.cpp's ScopedCacheHome,
// generalized to any variable name.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::string_view value) : name_(name) {
        if (const char* prev = std::getenv(name); prev != nullptr) {
            previous_ = prev;
        }
        ::setenv(name_, std::string(value).c_str(), 1);
    }
    ~ScopedEnv() {
        if (previous_.has_value()) {
            ::setenv(name_, previous_->c_str(), 1);
        } else {
            ::unsetenv(name_);
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

// Unsets an environment variable for the test's lifetime, restoring it
// afterward if it was set.
class ScopedUnsetEnv {
public:
    explicit ScopedUnsetEnv(const char* name) : name_(name) {
        if (const char* prev = std::getenv(name); prev != nullptr) {
            previous_ = prev;
        }
        ::unsetenv(name_);
    }
    ~ScopedUnsetEnv() {
        if (previous_.has_value()) {
            ::setenv(name_, previous_->c_str(), 1);
        }
    }
    ScopedUnsetEnv(const ScopedUnsetEnv&) = delete;
    ScopedUnsetEnv& operator=(const ScopedUnsetEnv&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Project make_project(const TempDir& dir, std::string_view tool = "") {
    Config cfg{.name = "demo", .cpp_standard = 23, .build_tool = std::string(tool)};
    REQUIRE(cup::project::write_config(dir.path(), cfg).has_value());
    return Project{dir.path(), cfg};
}

// index_of_builtin_kind returns the 1-based position of kind among
// tmpl::builtin_kinds(family) (template_new's own picker list, which -
// unlike tmpl::kinds - is not filtered to exclude app/project/test), so a
// numbered-select feed line doesn't have to hardcode the corpus's ordering.
std::size_t index_of_builtin_kind(std::string_view family, std::string_view kind) {
    const auto kinds = cup::tmpl::builtin_kinds(family);
    const auto it = std::find(kinds.begin(), kinds.end(), kind);
    REQUIRE(it != kinds.end());
    return static_cast<std::size_t>(std::distance(kinds.begin(), it)) + 1;
}

}

// --- template.go --------------------------------------------------------

TEST_CASE("template_list runs without error and needs no project template dir",
          "[cmd][template]") {
    const TempDir dir;
    make_project(dir);
    const ScopedCwd cwd(dir.path());
    REQUIRE(cup::cmd::template_list().has_value());
}

TEST_CASE("template_new copies a builtin kind into .cup/templates/<name>", "[cmd][template]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    const ScopedCwd cwd(dir.path());
    const std::size_t class_index = index_of_builtin_kind("modules", "class");

    const ScopedStdin not_a_terminal("");
    std::istringstream in(std::to_string(class_index) + "\n\n");
    const cup::ui::ScopedInput scoped(in);

    auto result = cup::cmd::template_new(std::vector<std::string>{"my-class"});
    REQUIRE(result.has_value());

    const auto dst = proj.root / ".cup" / "templates" / "my-class";
    REQUIRE(std::filesystem::is_directory(dst));
    REQUIRE_FALSE(std::filesystem::is_empty(dst));
}

TEST_CASE("template_new declines to overwrite an existing kind unless confirmed",
          "[cmd][template]") {
    const TempDir dir;
    const Project proj = make_project(dir);
    const ScopedCwd cwd(dir.path());
    const std::size_t class_index = index_of_builtin_kind("modules", "class");

    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in(std::to_string(class_index) + "\n\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::template_new(std::vector<std::string>{"my-class"}).has_value());
    }

    const auto marker = proj.root / ".cup" / "templates" / "my-class" / "marker";
    std::ofstream(marker) << "untouched";

    {
        const ScopedStdin not_a_terminal("");
        std::istringstream in(std::to_string(class_index) + "\n\nn\n");
        const cup::ui::ScopedInput scoped(in);
        REQUIRE(cup::cmd::template_new(std::vector<std::string>{"my-class"}).has_value());
    }
    REQUIRE(read_whole_file(marker) == "untouched");
}

TEST_CASE("run_template dispatches list/new and rejects an unknown subcommand",
          "[cmd][template]") {
    const TempDir dir;
    make_project(dir);
    const ScopedCwd cwd(dir.path());

    REQUIRE(cup::cmd::run_template({}).has_value());

    auto bad = cup::cmd::run_template(std::vector<std::string>{"bogus"});
    REQUIRE_FALSE(bad.has_value());
}

// --- completion.go -------------------------------------------------------

TEST_CASE("subcommand_names matches commands()", "[cmd][completion]") {
    const auto names = cup::cmd::subcommand_names();
    const auto cmds = cup::cmd::commands();
    REQUIRE(names.size() == cmds.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        REQUIRE(names[i] == cmds[i].name);
    }
}

TEST_CASE("script_for returns each shell's script and rejects an unsupported one",
          "[cmd][completion]") {
    REQUIRE(cup::cmd::script_for("bash").has_value());
    REQUIRE(cup::cmd::script_for("zsh").has_value());
    REQUIRE(cup::cmd::script_for("fish").has_value());
    auto bad = cup::cmd::script_for("powershell");
    REQUIRE_FALSE(bad.has_value());
}

TEST_CASE("bash/zsh/fish completions carry the dynamic command, category, and mode lists",
          "[cmd][completion]") {
    const std::string bash = cup::cmd::bash_completion();
    const std::string zsh = cup::cmd::zsh_completion();
    const std::string fish = cup::cmd::fish_completion();

    for (const auto& name : cup::cmd::subcommand_names()) {
        REQUIRE(bash.find(name) != std::string::npos);
        REQUIRE(zsh.find(name) != std::string::npos);
    }
    REQUIRE(bash.find("configure|build|rebuild|run|test|retest") != std::string::npos);
    REQUIRE(zsh.find("configure|build|rebuild|run|test|retest") != std::string::npos);
    REQUIRE(bash.find("Debug Release Coverage") != std::string::npos);
    REQUIRE(bash.find("app lib test third-party") != std::string::npos);
    REQUIRE(fish.find("complete -c cup -f") != std::string::npos);
    REQUIRE(fish.find("__fish_seen_subcommand_from add") != std::string::npos);
}

TEST_CASE("detect_shell reads $SHELL, defaulting to bash", "[cmd][completion]") {
    {
        const ScopedEnv shell("SHELL", "/usr/bin/zsh");
        REQUIRE(cup::cmd::detect_shell() == "zsh");
    }
    {
        const ScopedEnv shell("SHELL", "/usr/bin/fish");
        REQUIRE(cup::cmd::detect_shell() == "fish");
    }
    {
        const ScopedEnv shell("SHELL", "/bin/bash");
        REQUIRE(cup::cmd::detect_shell() == "bash");
    }
    {
        const ScopedUnsetEnv shell("SHELL");
        REQUIRE(cup::cmd::detect_shell() == "bash");
    }
}

TEST_CASE("run_completion prints a script for a supported shell and rejects garbage",
          "[cmd][completion]") {
    REQUIRE(cup::cmd::run_completion(std::vector<std::string>{"bash"}).has_value());
    REQUIRE(cup::cmd::run_completion(std::vector<std::string>{"zsh"}).has_value());
    REQUIRE(cup::cmd::run_completion(std::vector<std::string>{"fish"}).has_value());
    REQUIRE_FALSE(cup::cmd::run_completion(std::vector<std::string>{"powershell"}).has_value());
    REQUIRE_FALSE(cup::cmd::run_completion({}).has_value());
}

TEST_CASE("run_completion install (bash) writes under XDG_DATA_HOME", "[cmd][completion][install]") {
    const TempDir home;
    const TempDir data_home;
    const ScopedEnv home_env("HOME", home.path().string());
    const ScopedEnv xdg_env("XDG_DATA_HOME", data_home.path().string());

    auto result = cup::cmd::run_completion(std::vector<std::string>{"install", "bash"});
    REQUIRE(result.has_value());

    const auto dest = data_home.path() / "bash-completion" / "completions" / "cup";
    REQUIRE(std::filesystem::exists(dest));
    REQUIRE(read_whole_file(dest) == cup::cmd::bash_completion());
}

TEST_CASE("run_completion install (fish) writes under ~/.config/fish/completions",
          "[cmd][completion][install]") {
    const TempDir home;
    const ScopedEnv home_env("HOME", home.path().string());

    auto result = cup::cmd::run_completion(std::vector<std::string>{"install", "fish"});
    REQUIRE(result.has_value());

    const auto dest = home.path() / ".config" / "fish" / "completions" / "cup.fish";
    REQUIRE(std::filesystem::exists(dest));
    REQUIRE(read_whole_file(dest) == cup::cmd::fish_completion());
}

TEST_CASE("run_completion install (zsh) writes _cup and idempotently wires ~/.zshrc's fpath",
          "[cmd][completion][install]") {
    const TempDir home;
    const ScopedEnv home_env("HOME", home.path().string());

    REQUIRE(cup::cmd::run_completion(std::vector<std::string>{"install", "zsh"}).has_value());

    const auto dest = home.path() / ".zsh" / "completions" / "_cup";
    REQUIRE(std::filesystem::exists(dest));
    REQUIRE(read_whole_file(dest) == cup::cmd::zsh_completion());

    const auto zshrc = home.path() / ".zshrc";
    const auto first = read_whole_file(zshrc);
    REQUIRE(first.has_value());
    REQUIRE(first->find("# cup completion") != std::string::npos);
    REQUIRE(first->find("compinit") != std::string::npos);

    // A second install must not duplicate the fpath block.
    REQUIRE(cup::cmd::run_completion(std::vector<std::string>{"install", "zsh"}).has_value());
    const auto second = read_whole_file(zshrc);
    REQUIRE(second == first);
}

TEST_CASE("run_completion install with no shell falls back to $SHELL", "[cmd][completion][install]") {
    const TempDir home;
    const ScopedEnv home_env("HOME", home.path().string());
    const ScopedEnv shell_env("SHELL", "/usr/bin/fish");

    REQUIRE(cup::cmd::run_completion(std::vector<std::string>{"install"}).has_value());
    REQUIRE(std::filesystem::exists(home.path() / ".config" / "fish" / "completions" / "cup.fish"));
}
