"""Shared helpers of the project-flow tooling (tools/flow/): configuration, commands, GitHub token.

The configuration is flow.toml at the root of the repository (written by /project-init). On a deploy machine the
tools read RELEASE.json instead: the package embeds the parts of flow.toml the machine needs, so the machine needs
neither tomllib nor the repository.

Standard library only (Python 3.11+ on the developer's machine for tomllib; 3.8+ on a deploy machine).
"""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

DEFAULTS: dict = {
    "project": {"repository": "", "default_branch": "main"},
    "boards": {"owner": "", "po": 0, "dev": 0, "qa": 0},
    "tickets": {
        "pattern": r"(?:[\w.-]+/[\w.-]+)?#\d+",
        "exempt": [r"^Merge ", r'^Revert "', r"^(fixup|squash|amend)! "],
    },
    "release": {"branch_prefix": "release/", "tag_prefix": "v", "package": "", "qa_done": "Ready to deploy"},
    "build": {"command": ""},
    "package": {"include": [], "exclude": ["**/__pycache__/**", "**/*.pyc"], "external": []},
    "database": {"engine": "", "dirs": ["database"], "url_env": "DATABASE_URL", "table": "flow_migrations"},
    "deploy": {"path": "", "shared": ["secrets.env"], "post_unpack": [], "restart": "", "keep": 5},
}


class FlowError(Exception):
    pass


def merge(base: dict, over: dict) -> dict:
    out = dict(base)
    for key, value in over.items():
        out[key] = merge(base[key], value) if isinstance(value, dict) and isinstance(base.get(key), dict) else value
    return out


def load_config(root: Path = ROOT) -> dict:
    """flow.toml merged over the defaults; on a deploy machine, the `flow` section of RELEASE.json."""
    release = root / "RELEASE.json"
    toml_path = root / "flow.toml"
    if toml_path.is_file():
        import tomllib

        with open(toml_path, "rb") as f:
            data = tomllib.load(f)
    elif release.is_file():
        data = json.loads(release.read_text(encoding="utf-8")).get("flow", {})
    else:
        raise FlowError(f"ni flow.toml ni RELEASE.json dans {root} (lancez /project-init)")
    config = merge(DEFAULTS, data)
    if not config["release"]["package"] and config["project"]["repository"]:
        config["release"]["package"] = config["project"]["repository"].split("/")[-1].lower()
    return config


def run(*command: str, cwd: Path = ROOT, check: bool = True, input: str | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, input=input)
    if check and result.returncode:
        raise FlowError(f"{' '.join(command)} : {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout.strip()


def token() -> str:
    for key in ("GH_TOKEN", "GITHUB_TOKEN"):
        if os.environ.get(key):
            return os.environ[key]
    try:
        return subprocess.run(["gh", "auth", "token"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as e:
        raise FlowError("pas de token GitHub : GH_TOKEN, ou `gh auth login` (scopes repo et project)") from e


def read_env_file(path: Path) -> dict[str, str]:
    """KEY=VALUE lines (secrets.env); comments and blank lines skipped, surrounding quotes removed."""
    env: dict[str, str] = {}
    if not path.is_file():
        return env
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip().removeprefix("export ").strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        env[key] = value
    return env


def utf8_stdout() -> None:
    import sys

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
        sys.stderr.reconfigure(encoding="utf-8")
