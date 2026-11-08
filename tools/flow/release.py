"""Releases and deploys — just release, release-freeze, release-package, deploy, deploy-rollback, deploy-status.

    python3 tools/flow/release.py start <X.Y.Z> [--dry-run]            # branch release/X.Y.Z, Dev Done → QA
    python3 tools/flow/release.py freeze <X.Y.Z> [--force] [--dry-run] # QA validated → tag vX.Y.Z
    python3 tools/flow/release.py package <version> [--out DIR]        # the archive of a version (Actions)
    python3 tools/flow/release.py deploy <version> <ssh-host> [--no-restart] [--dry-run]
    python3 tools/flow/release.py rollback <ssh-host> [--no-restart]
    python3 tools/flow/release.py status <ssh-host>

The cycle of a release:
1. `start X.Y.0` cuts release/X.Y.0 from origin/<default branch>: features are frozen, only bug fixes land on it.
   `start X.Y.Z` (Z > 0, a fix of a version in production) cuts release/X.Y.Z from the last frozen tag of X.Y.
   The Dev Done tickets go to QA. A version is used once: a tag, a release branch or a GitHub Release refuses it.
2. Every push on the branch builds a release candidate (workflow « Release package »): tag and prerelease
   vX.Y.Z-rc.N with its package, deployed on the preprod for QA.
3. `freeze X.Y.Z` once QA validated: refused while a QA ticket is not in [release] qa_done, or when the head of the
   branch has no release candidate (QA did not test that commit) — unless --force. It tags vX.Y.Z on the head of the
   branch; the workflow builds the production package on that tag.

The package ([package] of flow.toml) holds what runs and nothing else: the built files of [package] include, the
database steps and their reverts, the tools the machine runs (tools/flow/deploy_remote.py, dbsteps.py, flowlib.py)
and RELEASE.json (version, commit, the flow settings the machine needs, the third-party artifacts). A third-party
binary is not inside: [[package.external]] names its URL and SHA-256, and the machine downloads it and checks it.

Standard library only (plus git, gh, ssh and scp on the developer's machine).
"""

from __future__ import annotations

import argparse
import fnmatch
import io
import json
import re
import subprocess
import sys
import tarfile
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flowlib  # noqa: E402
from flowlib import FlowError, run  # noqa: E402

ROOT = flowlib.ROOT
SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
VERSION = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-rc\.[1-9]\d*)?$")
MACHINE_TOOLS = ["tools/flow/flowlib.py", "tools/flow/dbsteps.py", "tools/flow/deploy_remote.py"]


def settings(config: dict) -> dict:
    r, d = config["release"], config["deploy"]
    return {
        "branch": r["branch_prefix"], "tag": r["tag_prefix"], "package": r["package"], "qa_done": r["qa_done"],
        "main": config["project"]["default_branch"], "repo": config["project"]["repository"],
        "path": d["path"], "keep": int(d["keep"]),
    }


# ---------------------------------------------------------------------------------------------------------------
# Start
# ---------------------------------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Plan:
    version: str
    branch: str
    base: str  # the default branch, or the tag of the version it fixes


def frozen(tags: set[str], s: dict) -> list[tuple[int, int, int]]:
    out = []
    for tag in tags:
        m = SEMVER.match(tag[len(s["tag"]):]) if tag.startswith(s["tag"]) else None
        if m:
            out.append(tuple(int(g) for g in m.groups()))
    return sorted(out)


def check_version(version: str, tags: set[str], branches: set[str], releases: set[str], s: dict) -> Plan:
    m = SEMVER.match(version)
    if not m:
        raise FlowError(f"{version!r} n'est pas une version X.Y.Z (ex. 0.1.0)")
    tag, branch = s["tag"] + version, s["branch"] + version
    used = [what for what, hit in (
        (f"le tag {tag}", tag in tags),
        (f"la branche {branch}", branch in branches),
        (f"la GitHub Release {tag}", tag in releases),
    ) if hit]
    if used:
        raise FlowError(f"la version {version} est déjà utilisée : {', '.join(used)}")
    major, minor, patch = (int(g) for g in m.groups())
    if patch == 0:
        return Plan(version, branch, s["main"])
    before = [v for v in frozen(tags, s) if v[:2] == (major, minor) and v[2] < patch]
    if not before:
        raise FlowError(f"{version} corrige une version {major}.{minor}.x en production, mais aucune n'a été figée "
                        f"(pas de tag {s['tag']}{major}.{minor}.*)")
    return Plan(version, branch, s["tag"] + ".".join(map(str, before[-1])))


def used_versions(s: dict) -> tuple[set[str], set[str], set[str]]:
    run("git", "fetch", "--quiet", "--tags", "origin")
    tags = set(run("git", "tag", "--list").split())
    tags |= {line.split("refs/tags/")[1].removesuffix("^{}") for line in
             run("git", "ls-remote", "--tags", "origin").splitlines() if "refs/tags/" in line}
    branches = {line.split("refs/heads/")[1] for line in
                run("git", "ls-remote", "--heads", "origin", f"{s['branch']}*").splitlines() if "refs/heads/" in line}
    releases = {r["tagName"] for r in json.loads(
        run("gh", "release", "list", "--repo", s["repo"], "--limit", "1000", "--json", "tagName") or "[]")}
    return tags, branches, releases


def cmd_start(version: str, dry_run: bool) -> int:
    s = settings(flowlib.load_config())
    plan = check_version(version, *used_versions(s), s)
    base = f"origin/{plan.base}" if plan.base == s["main"] else f"{plan.base}^{{commit}}"
    commit = run("git", "rev-parse", base)
    print(f"{'[dry-run] ' if dry_run else ''}branche {plan.branch} depuis {plan.base} ({commit[:7]}) : "
          f"chaque push y construit une release candidate {version}-rc.N")
    if not dry_run:
        run("git", "push", "origin", f"{commit}:refs/heads/{plan.branch}")
    import boards_sync
    print("boards : Dev Done → QA Release ready")
    code = boards_sync.main(["--release"] + (["--dry-run"] if dry_run else []))
    if not dry_run:
        where = (f", à déployer sur la préprod (just deploy {version}-rc.1 <hôte>)" if s["path"]
                 else ", à tester par la QA")
        print(f"release {version} en QA : https://github.com/{s['repo']}/actions construit {version}-rc.1{where}. "
              f"Correctifs : PR vers {plan.branch}. QA validée : just release-freeze {version}")
    return code


# ---------------------------------------------------------------------------------------------------------------
# Freeze
# ---------------------------------------------------------------------------------------------------------------

def check_freeze(version: str, tags: set[str], branches: set[str], qa: dict[str, str | None], head_rcs: list[str],
                 s: dict, force: bool = False) -> list[str]:
    """Why `freeze` refuses (empty: it can tag). `qa`: label → Status of the QA tickets; `head_rcs`: the release
    candidate tags on the head of the branch. --force turns the QA reasons into warnings."""
    if not SEMVER.match(version):
        raise FlowError(f"{version!r} n'est pas une version X.Y.Z")
    tag, branch = s["tag"] + version, s["branch"] + version
    if tag in tags:
        raise FlowError(f"{version} est déjà figée : le tag {tag} existe")
    if branch not in branches:
        raise FlowError(f"la branche {branch} n'existe pas (just release {version})")
    reasons = []
    pending = sorted(f"{label} ({status or 'sans statut'})" for label, status in qa.items() if status != s["qa_done"])
    if pending:
        reasons.append(f"{len(pending)} ticket(s) QA pas en {s['qa_done']} : {', '.join(pending)}")
    if not head_rcs:
        reasons.append(f"le dernier commit de {branch} n'a pas de release candidate : la QA n'a pas testé ce commit")
    if reasons and not force:
        raise FlowError("freeze refusé — " + " ; ".join(reasons) + " (--force pour passer outre)")
    return reasons


def qa_board(config: dict) -> dict[str, str | None]:
    import boards_sync
    gh = boards_sync.client(config)
    _, _, _, cards, labels = gh.board(int(config["boards"]["qa"]))
    return {labels[issue]: card.status for issue, card in cards.items()}


def cmd_freeze(version: str, force: bool, dry_run: bool) -> int:
    config = flowlib.load_config()
    s = settings(config)
    tags, branches, _ = used_versions(s)
    branch = s["branch"] + version
    head = run("git", "rev-parse", f"origin/{branch}") if branch in branches else ""
    head_rcs = run("git", "tag", "--points-at", head, "--list", f"{s['tag']}{version}-rc.*").split() if head else []
    for warning in check_freeze(version, tags, branches, qa_board(config), head_rcs, s, force):
        print(f"attention (--force) : {warning}")
    tag = s["tag"] + version
    tested = f" (testé en {', '.join(head_rcs)})" if head_rcs else ""
    print(f"{'[dry-run] ' if dry_run else ''}tag {tag} sur {branch} ({head[:7]}){tested} : le workflow construit le "
          f"package de production")
    if not dry_run:
        run("git", "tag", "-a", tag, head, "-m", f"Release {version}")
        run("git", "push", "origin", tag)
        then = f", puis just deploy {version} <hôte>" if s["path"] else ""
        print(f"{version} figée : https://github.com/{s['repo']}/releases/tag/{tag} une fois le package construit"
              f"{then}. Reportez les correctifs : PR {branch} → {s['main']}.")
    return 0


# ---------------------------------------------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------------------------------------------

def excluded(rel: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatch(rel, p) or fnmatch.fnmatch("/" + rel, p) for p in patterns)


def package_files(root: Path, config: dict) -> list[Path]:
    """The files of [package] include (globs allowed, a folder is taken whole), minus [package] exclude, plus the
    database steps and reverts and the machine tools. A pattern that matches nothing is an error: a build is missing."""
    patterns = list(config["package"]["include"])
    if not patterns:
        raise FlowError("flow.toml [package] include est vide : listez les fichiers construits à livrer")
    files: set[Path] = set()
    for pattern in patterns:
        matched = list(root.glob(pattern)) if any(c in pattern for c in "*?[") else [root / pattern]
        matched = [p for p in matched if p.exists()]
        if not matched:
            raise FlowError(f"[package] include « {pattern} » ne correspond à aucun fichier (build lancé ?)")
        for p in matched:
            files.update([p] if p.is_file() else [x for x in p.rglob("*") if x.is_file()])
    if config["database"]["engine"]:
        import dbsteps

        found = dbsteps.problems(root, config)
        if found:
            raise FlowError("on ne pourrait pas revenir à la version précédente — " + " ; ".join(found))
        for d in config["database"]["dirs"]:
            folder = root / d
            if folder.is_dir():
                files.update(p for p in folder.glob("*.sql") if dbsteps.STEP.match(p.name) or dbsteps.REVERT.match(p.name))
    # Without a deploy machine (no [deploy] path), tools/flow/ has no deploy_remote.py: ship the tools present.
    files.update(root / t for t in MACHINE_TOOLS if (root / t).is_file())
    exclude = config["package"]["exclude"]
    return sorted(f for f in files if not excluded(f.relative_to(root).as_posix(), exclude) and ".git" not in f.parts)


def machine_config(config: dict) -> dict:
    """What RELEASE.json carries of flow.toml: what the machine needs, nothing else."""
    return {
        "project": {"repository": config["project"]["repository"]},
        "release": {"package": config["release"]["package"]},
        "database": config["database"],
        "deploy": config["deploy"],
        "package": {"external": config["package"]["external"]},
    }


def build_package(version: str, out: Path, root: Path = ROOT) -> Path:
    if not VERSION.match(version):
        raise FlowError(f"{version!r} n'est pas une version X.Y.Z ou X.Y.Z-rc.N")
    config = flowlib.load_config(root)
    for ext in config["package"]["external"]:
        missing = [k for k in ("name", "url", "sha256", "dest") if not ext.get(k)]
        if missing:
            raise FlowError(f"[[package.external]] {ext.get('name', '?')} : {', '.join(missing)} manquant(s)")
    files = package_files(root, config)
    commit = run("git", "rev-parse", "HEAD", cwd=root, check=False) or "unknown"
    release = {
        "version": version,
        "commit": commit,
        "built_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "files": len(files),
        "flow": machine_config(config),
    }
    name = f"{config['release']['package']}-{version}"
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f"{name}.tar.gz"

    def info(arcname: str, size: int, mode: int = 0o644) -> tarfile.TarInfo:
        ti = tarfile.TarInfo(f"{name}/{arcname}")
        ti.size, ti.mode, ti.mtime = size, mode, 0
        return ti

    with tarfile.open(archive, "w:gz") as tar:
        data = (json.dumps(release, indent=2, ensure_ascii=False) + "\n").encode()
        tar.addfile(info("RELEASE.json", len(data)), io.BytesIO(data))
        for path in files:
            rel = path.relative_to(root).as_posix()
            executable = path.suffix in (".py", ".sh") or bool(path.stat().st_mode & 0o111)
            with open(path, "rb") as f:
                tar.addfile(info(rel, path.stat().st_size, 0o755 if executable else 0o644), f)
    digest = sha256_of(archive)
    (out / f"{name}.tar.gz.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    externals = len(config["package"]["external"])
    print(f"{archive} : {len(files) + 1} fichiers, {archive.stat().st_size / 1e3:.0f} ko"
          + (f", {externals} artefact(s) tiers par référence" if externals else ""))
    return archive


def sha256_of(path: Path) -> str:
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------------------------------------------
# Deploy (over SSH: tools/flow/deploy_remote.py runs on the machine, sent on stdin)
# ---------------------------------------------------------------------------------------------------------------

def shell_quote(value: str) -> str:
    return "'" + value.replace("'", "'\\''") + "'"


def remote(host: str, args: list[str], dry_run: bool = False) -> int:
    script = (ROOT / "tools" / "flow" / "deploy_remote.py").read_text(encoding="utf-8")
    command = ["ssh", host, "python3", "-", *[shell_quote(a) for a in args]]
    if dry_run:
        print("[dry-run] " + " ".join(command) + " < tools/flow/deploy_remote.py")
        return 0
    return subprocess.run(command, input=script, text=True).returncode


def download(version: str, s: dict) -> Path:
    target = ROOT / ".flow" / "releases"
    target.mkdir(parents=True, exist_ok=True)
    name = f"{s['package']}-{version}.tar.gz"
    run("gh", "release", "download", s["tag"] + version, "--repo", s["repo"], "--dir", str(target), "--clobber",
        "--pattern", name, "--pattern", f"{name}.sha256")
    archive = target / name
    expected = (target / f"{name}.sha256").read_text(encoding="utf-8").split()[0]
    if sha256_of(archive) != expected:
        raise FlowError(f"{name} : le SHA-256 ne correspond pas à {name}.sha256")
    return archive


def base_path(s: dict) -> str:
    if not s["path"]:
        raise FlowError("flow.toml [deploy] path est vide : le dossier de l'application sur la machine")
    return s["path"]


def cmd_deploy(version: str, host: str, restart: bool, dry_run: bool) -> int:
    s = settings(flowlib.load_config())
    if not VERSION.match(version):
        raise FlowError(f"{version!r} n'est pas une version X.Y.Z ou X.Y.Z-rc.N")
    base = base_path(s)
    archive = download(version, s)
    print(f"{archive.name} : téléchargé et vérifié")
    incoming = f"{base}/incoming/{archive.name}"
    if dry_run:
        print(f"[dry-run] scp {archive.name} {host}:{incoming}")
    else:
        if subprocess.run(["ssh", host, f"mkdir -p {shell_quote(base + '/incoming')}"]).returncode:
            raise FlowError(f"ssh {host} : connexion impossible")
        if subprocess.run(["scp", "-q", str(archive), f"{host}:{incoming}"]).returncode:
            raise FlowError(f"scp vers {host} : échec")
    args = ["deploy", base, incoming, "--keep", str(s["keep"])]
    if not restart:
        args.append("--no-restart")
    return remote(host, args, dry_run)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("start", help="branche release/X.Y.Z, Dev Done → QA")
    p.add_argument("version")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("freeze", help="QA validée : tag vX.Y.Z, package de production")
    p.add_argument("version")
    p.add_argument("--force", action="store_true", help="figer malgré des tickets QA non validés ou un commit non testé")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("package", help="construit le package d'une version")
    p.add_argument("version")
    p.add_argument("--out", default=str(ROOT / ".flow" / "packages"))
    p = sub.add_parser("deploy", help="installe une release sur une machine (SSH)")
    p.add_argument("version")
    p.add_argument("host", help="destination ssh (alias de ~/.ssh/config ou user@machine)")
    p.add_argument("--no-restart", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("rollback", help="revient à la release précédente")
    p.add_argument("host")
    p.add_argument("--no-restart", action="store_true")
    p = sub.add_parser("status", help="releases installées et release courante")
    p.add_argument("host")
    flowlib.utf8_stdout()  # before argparse: --help is French
    args = parser.parse_args(argv)
    try:
        if args.command == "start":
            return cmd_start(args.version, args.dry_run)
        if args.command == "freeze":
            return cmd_freeze(args.version, args.force, args.dry_run)
        if args.command == "package":
            build_package(args.version, Path(args.out))
            return 0
        if args.command == "deploy":
            return cmd_deploy(args.version, args.host, not args.no_restart, args.dry_run)
        base = base_path(settings(flowlib.load_config()))
        if args.command == "rollback":
            return remote(args.host, ["rollback", base] + (["--no-restart"] if args.no_restart else []))
        return remote(args.host, ["status", base])
    except FlowError as e:
        print(f"release : {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
