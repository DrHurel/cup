module;
#include <array>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
module cup.cmd;

import cup.platform;
import cup.ui;

namespace cup::cmd {
namespace {

// modeCommands are the subcommands that take a leading build MODE argument.
constexpr std::array<std::string_view, 6> kModeCommands{"configure", "build", "rebuild",
                                                        "run",       "test",  "retest"};

// kInitFlags are `cup init`'s flags (see Init.cpp's parse_init_args).
constexpr std::array<std::string_view, 2> kInitFlags{"--std", "--build-tool"};

// ps_list renders strings as a PowerShell array literal: @('a', 'b').
template <typename Range>
std::string ps_list(const Range& parts) {
    std::string out = "@(";
    bool first = true;
    for (const auto& p : parts) {
        if (!first) {
            out += ", ";
        }
        out += "'";
        out += p;
        out += "'";
        first = false;
    }
    out += ")";
    return out;
}

template <typename Range>
std::string join(const Range& parts, std::string_view sep) {
    std::string out;
    bool first = true;
    for (const auto& p : parts) {
        if (!first) {
            out += sep;
        }
        out += p;
        first = false;
    }
    return out;
}

std::string fish_quote(std::string_view s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') {
            out += "\\'";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::optional<std::string> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// data_home returns $XDG_DATA_HOME or its ~/.local/share default.
std::string data_home(std::string_view home) {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return xdg;
    }
    return (std::filesystem::path(home) / ".local" / "share").string();
}

// ensure_zsh_fpath makes sure ~/.zshrc puts dir on fpath and runs compinit,
// so the installed _cup function is picked up automatically. It appends an
// idempotent, marker-guarded block only when one is not already present.
std::expected<void, error::Error> ensure_zsh_fpath(std::string_view home,
                                                    const std::filesystem::path& dir) {
    const std::filesystem::path zshrc = std::filesystem::path(home) / ".zshrc";
    constexpr std::string_view kMarker = "# cup completion";
    if (const auto existing = read_whole_file(zshrc);
        existing.has_value() && existing->contains(kMarker)) {
        return {};
    }
    const std::string block =
        std::format("\n{}\nfpath=(\"{}\" $fpath)\nautoload -Uz compinit && compinit\n", kMarker,
                    dir.string());
    std::ofstream out(zshrc, std::ios::app);
    if (!out) {
        return std::unexpected(error::Error(std::format("opening {}", zshrc.string())));
    }
    out << block;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", zshrc.string())));
    }
    ui::updated(zshrc.string());
    return {};
}

std::expected<void, error::Error> install_completion(std::span<const std::string> args) {
    std::string shell = args.empty() ? "" : args[0];
    if (shell.empty()) {
        shell = detect_shell();
    }

    auto script = script_for(shell);
    if (!script.has_value()) {
        return std::unexpected(std::move(script).error());
    }
    // PowerShell has no per-user completion directory to drop a file into:
    // completers live in the profile script, whose path only PowerShell
    // itself knows ($PROFILE moves with a redirected Documents folder).
    if (shell == "powershell") {
        return std::unexpected(error::Error(
            "PowerShell has no completion directory to install into — add this line to your "
            "$PROFILE instead:  cup completion powershell | Out-String | Invoke-Expression"));
    }

    const std::filesystem::path home = platform::home_dir();
    if (home.empty()) {
        return std::unexpected(error::Error("$HOME is not set"));
    }

    // dest is a file each shell auto-loads on startup: the per-user
    // completion directory for bash and fish, and an fpath directory we
    // register for zsh.
    std::filesystem::path dest;
    if (shell == "bash") {
        dest = std::filesystem::path(data_home(home.string())) / "bash-completion" / "completions" /
               "cup";
    } else if (shell == "fish") {
        dest = home / ".config" / "fish" / "completions" / "cup.fish";
    } else if (shell == "zsh") {
        const std::filesystem::path dir = home / ".zsh" / "completions";
        dest = dir / "_cup";
        if (auto ensured = ensure_zsh_fpath(home.string(), dir); !ensured.has_value()) {
            return std::unexpected(std::move(ensured).error());
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
        return std::unexpected(error::Error(
            std::format("creating {}: {}", dest.parent_path().string(), ec.message())));
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    out << *script;
    if (!out) {
        return std::unexpected(error::Error(std::format("writing {}", dest.string())));
    }

    ui::wrote(dest.string());
    ui::success(std::format("cup {} completion installed — open a new shell to use it.", shell));
    return {};
}

}  // namespace

std::vector<std::string> subcommand_names() {
    std::vector<std::string> names;
    names.reserve(commands().size());
    for (const auto& c : commands()) {
        names.push_back(c.name);
    }
    return names;
}

std::string detect_shell() {
    std::string base;
    if (const char* shell_env = std::getenv("SHELL"); shell_env != nullptr) {
        base = std::filesystem::path(shell_env).filename().string();
    }
    if (base == "zsh") {
        return "zsh";
    }
    if (base == "fish") {
        return "fish";
    }
#ifdef _WIN32
    // A native Windows shell (cmd, PowerShell) sets no $SHELL; MSYS2 and Git
    // Bash do, and were handled above.
    if (base.empty()) {
        return "powershell";
    }
#endif
    return "bash";
}

std::string bash_completion() {
    std::string out;
    out += "# cup bash completion.\n";
    out += "# Install it automatically with:  cup completion install\n";
    out += "# Or load it for one shell with:  source <(cup completion bash)\n";
    out += "_cup() {\n";
    out += "    local cur cmd\n";
    out += "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n";
    out += "    if [ \"$COMP_CWORD\" -eq 1 ]; then\n";
    out += "        COMPREPLY=( $(compgen -W \"" + join(subcommand_names(), " ") + "\" -- \"$cur\") )\n";
    out += "        return\n";
    out += "    fi\n";
    out += "    cmd=\"${COMP_WORDS[1]}\"\n";
    out += "    case \"$cmd\" in\n";
    out += "        add) COMPREPLY=( $(compgen -W \"" + join(kCategories, " ") + "\" -- \"$cur\") );;\n";
    out += "        " + join(kModeCommands, "|") + ") COMPREPLY=( $(compgen -W \"" +
           join(kBuildModes, " ") + "\" -- \"$cur\") );;\n";
    out += "        init) COMPREPLY=( $(compgen -W \"" + join(kInitFlags, " ") + "\" -- \"$cur\") );;\n";
    out += "        compiler) COMPREPLY=( $(compgen -W \"show set verify\" -- \"$cur\") );;\n";
    out += "        docker) COMPREPLY=( $(compgen -W \"new build push\" -- \"$cur\") );;\n";
    out += "        template) COMPREPLY=( $(compgen -W \"list new\" -- \"$cur\") );;\n";
    out += "        completion) COMPREPLY=( $(compgen -W \"bash zsh fish powershell install\" -- \"$cur\") );;\n";
    out += "    esac\n";
    out += "}\n";
    out += "complete -F _cup cup\n";
    return out;
}

std::string zsh_completion() {
    std::string out;
    out += "#compdef cup\n";
    out += "# cup zsh completion.\n";
    out += "# Install it automatically with:  cup completion install\n";
    out += "# Or load it for one shell with:  source <(cup completion zsh)\n";
    out += "_cup() {\n";
    out += "    if (( CURRENT == 2 )); then\n";
    out += "        compadd -- " + join(subcommand_names(), " ") + "\n";
    out += "        return\n";
    out += "    fi\n";
    out += "    case ${words[2]} in\n";
    out += "        add) compadd -- " + join(kCategories, " ") + ";;\n";
    out += "        " + join(kModeCommands, "|") + ") compadd -- " + join(kBuildModes, " ") + ";;\n";
    out += "        init) compadd -- " + join(kInitFlags, " ") + ";;\n";
    out += "        compiler) compadd -- show set verify;;\n";
    out += "        docker) compadd -- new build push;;\n";
    out += "        template) compadd -- list new;;\n";
    out += "        completion) compadd -- bash zsh fish powershell install;;\n";
    out += "    esac\n";
    out += "}\n";
    out += "compdef _cup cup\n";
    return out;
}

std::string fish_completion() {
    std::string out;
    out += "# cup fish completion.\n";
    out += "# Install it automatically with:  cup completion install\n";
    out += "complete -c cup -f\n";
    for (const auto& c : commands()) {
        out += "complete -c cup -n __fish_use_subcommand -a " + c.name + " -d " + fish_quote(c.summary) +
               "\n";
    }
    out += "complete -c cup -n '__fish_seen_subcommand_from add' -a '" + join(kCategories, " ") + "'\n";
    out += "complete -c cup -n '__fish_seen_subcommand_from " + join(kModeCommands, " ") + "' -a '" +
           join(kBuildModes, " ") + "'\n";
    out += "complete -c cup -n '__fish_seen_subcommand_from init' -a '" + join(kInitFlags, " ") + "'\n";
    out += "complete -c cup -n '__fish_seen_subcommand_from compiler' -a 'show set verify'\n";
    out += "complete -c cup -n '__fish_seen_subcommand_from docker' -a 'new build push'\n";
    out += "complete -c cup -n '__fish_seen_subcommand_from template' -a 'list new'\n";
    out += "complete -c cup -n '__fish_seen_subcommand_from completion' -a 'bash zsh fish powershell install'\n";
    return out;
}

std::string powershell_completion() {
    std::string out;
    out += "# cup PowerShell completion.\n";
    out += "# Load it for one session with:  cup completion powershell | Out-String | Invoke-Expression\n";
    out += "# Add that same line to your $PROFILE to load it in every session.\n";
    out += "Register-ArgumentCompleter -Native -CommandName cup, cup.exe -ScriptBlock {\n";
    out += "    param($wordToComplete, $commandAst, $cursorPosition)\n";
    out += "    $words = @($commandAst.CommandElements | ForEach-Object { $_.ToString() })\n";
    out += "    if ($words.Count -lt 2 -or ($words.Count -eq 2 -and $wordToComplete -ne '')) {\n";
    out += "        $candidates = " + ps_list(subcommand_names()) + "\n";
    out += "    } else {\n";
    out += "        $candidates = switch ($words[1]) {\n";
    out += "            'add' { " + ps_list(kCategories) + " }\n";
    out += "            'init' { " + ps_list(kInitFlags) + " }\n";
    out += "            'compiler' { @('show', 'set', 'verify') }\n";
    out += "            'docker' { @('new', 'build', 'push') }\n";
    out += "            'template' { @('list', 'new') }\n";
    out += "            'completion' { @('bash', 'zsh', 'fish', 'powershell', 'install') }\n";
    out += "            { $_ -in " + ps_list(kModeCommands) + " } { " + ps_list(kBuildModes) + " }\n";
    out += "            default { @() }\n";
    out += "        }\n";
    out += "    }\n";
    out += "    $candidates | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n";
    out += "        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n";
    out += "    }\n";
    out += "}\n";
    return out;
}

std::expected<std::string, error::Error> script_for(std::string_view shell) {
    if (shell == "bash") {
        return bash_completion();
    }
    if (shell == "zsh") {
        return zsh_completion();
    }
    if (shell == "fish") {
        return fish_completion();
    }
    if (shell == "powershell") {
        return powershell_completion();
    }
    return std::unexpected(error::Error(
        std::format("unsupported shell \"{}\" (want bash, zsh, fish, or powershell)", shell)));
}

std::expected<void, error::Error> run_completion(std::span<const std::string> args) {
    const std::string shell = args.empty() ? "" : args[0];
    if (shell == "install") {
        return install_completion(args.subspan(1));
    }
    if (shell == "bash") {
        ui::emit(bash_completion());
    } else if (shell == "zsh") {
        ui::emit(zsh_completion());
    } else if (shell == "fish") {
        ui::emit(fish_completion());
    } else if (shell == "powershell") {
        ui::emit(powershell_completion());
    } else {
        return std::unexpected(
            error::Error("usage: cup completion <bash|zsh|fish|powershell|install [shell]>"));
    }
    return {};
}

}
