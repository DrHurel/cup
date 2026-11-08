"""Every merged commit references its ticket(s) — the commit-msg hook, the « Ticket reference » workflow, just ticket-check.

    python3 tools/flow/ticket_check.py message <file>        # .githooks/commit-msg: the message being committed
    python3 tools/flow/ticket_check.py title "<PR title>"    # the PR title (the squash commit's subject)
    python3 tools/flow/ticket_check.py range <base> <head>   # every commit of base..head (a PR merged by rebase)

A reference matches [tickets] pattern of flow.toml (default `#123` or `owner/repo#123`), in the subject or the body.
Messages matching an [tickets] exempt pattern (merge commits, `Revert "…"`, fixup!/squash!) pass.
Standard library only.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flowlib  # noqa: E402

HELP = ("ajoutez la référence du ticket, ex. « Corrige le calcul du total (#42) » ou « Refs owner/repo#42 » "
        "— un commit par ticket si la PR en couvre plusieurs")


def strip_comments(message: str) -> str:
    """A message as git keeps it: no `#` comment lines (a commit template), no diff after the scissors line."""
    kept = []
    for line in message.splitlines():
        if line.startswith("# ------------------------ >8"):
            break
        if line.startswith("#") and not re.match(r"^#\d", line):
            continue
        kept.append(line)
    return "\n".join(kept).strip()


def references(message: str, config: dict) -> list[str]:
    return re.findall(config["tickets"]["pattern"], message)


def check(message: str, config: dict) -> str | None:
    """None when the message is fine, else why it is refused."""
    message = strip_comments(message)
    if not message:
        return None  # an empty message: git aborts the commit itself
    subject = message.splitlines()[0]
    if any(re.search(p, subject) for p in config["tickets"]["exempt"]):
        return None
    if references(message, config):
        return None
    return f"« {subject} » ne référence aucun ticket"


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    flowlib.utf8_stdout()
    if len(argv) < 2 or argv[0] not in ("message", "title", "range"):
        print(__doc__, file=sys.stderr)
        return 2
    config = flowlib.load_config()
    if argv[0] == "message":
        problems = [check(Path(argv[1]).read_text(encoding="utf-8"), config)]
    elif argv[0] == "title":
        problems = [check(" ".join(argv[1:]), config)]
    else:
        if len(argv) != 3:
            print("range <base> <head>", file=sys.stderr)
            return 2
        shas = flowlib.run("git", "rev-list", "--no-merges", f"{argv[1]}..{argv[2]}").split()
        problems = [(lambda p: p and f"{sha[:7]} {p}")(check(flowlib.run("git", "log", "-1", "--format=%B", sha), config))
                    for sha in shas]
    problems = [p for p in problems if p]
    for p in problems:
        print(f"ticket-check : {p}", file=sys.stderr)
    if problems:
        print(f"ticket-check : {HELP}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
