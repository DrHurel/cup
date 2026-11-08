"""One ticket across the three boards — the tool of the PO, Dev and QA roles (skills po, dev, qa).

    python3 tools/flow/ticket.py show <n>                         the ticket, its Status on each board, its PRs
                                                                  and the tickets linked to it
    python3 tools/flow/ticket.py list <po|dev|qa> [--status S]    the cards of a board
    python3 tools/flow/ticket.py move <n> <po|dev|qa> "<Status>" [--comment TEXT] [--dry-run]
    python3 tools/flow/ticket.py validate <n> [--comment TEXT] [--dry-run]
                                     PO: the feature merged on the default branch does what the ticket asks
                                     → Dev Done (it goes to QA with the next release)
    python3 tools/flow/ticket.py reopen <n> --reason TEXT [--only] [--dry-run]
                                     PO: it does not → the ticket and the tickets linked to it (closed by the same
                                     pull requests, sub-issues) are reopened, commented, and go back to Dev Ready to dev
    python3 tools/flow/ticket.py bug --title T --steps S --expected E --observed O --reproducibility R
                                     [--version V] [--environment ENV] [--notes N] [--for <n>] [--blocker] [--dry-run]
                                     QA: a bug found while testing → a ticket for the developers (type:bug,
                                     release-blocker with --blocker), QA Bug and Dev Ready to dev; --for: the ticket it
                                     breaks goes to QA Bug too. How to reproduce it and how often it reproduces are
                                     mandatory: that is the QA's deliverable.

What is written on GitHub (comments, bug tickets) is in the language of the repository: [project] language of
flow.toml, "fr" (default) or "en". Configuration and token: as boards_sync.py. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import boards_sync  # noqa: E402
import flowlib  # noqa: E402
from boards_sync import NAMES  # noqa: E402

READY, VALIDATED, DONE, BUG = "Ready to dev", "Validated", "Done", "Bug"
# What the roles write on GitHub, in the language of the repository ([project] language).
TEXT = {
    "fr": {
        "validated": "**PO — validé** sur la branche par défaut ({commits}).",
        "refused": "**PO — refusé**, retour en développement ({prs}).",
        "no_pr": "aucune PR mergée",
        "linked": "lié à #{number}, refusé par le PO : {reason}",
        "bug_comment": "**QA — bug** : #{number} {title}",
        "breaks": "Casse #{number}.",
        "version": "Version testée", "environment": "Environnement", "steps": "Étapes pour reproduire",
        "expected": "Attendu", "observed": "Observé", "reproducibility": "Reproductibilité", "notes": "Notes",
    },
    "en": {
        "validated": "**PO — validated** on the default branch ({commits}).",
        "refused": "**PO — refused**, back to development ({prs}).",
        "no_pr": "no merged PR",
        "linked": "linked to #{number}, refused by the PO: {reason}",
        "bug_comment": "**QA — bug**: #{number} {title}",
        "breaks": "Breaks #{number}.",
        "version": "Version tested", "environment": "Environment", "steps": "Steps to reproduce",
        "expected": "Expected", "observed": "Observed", "reproducibility": "Reproducibility", "notes": "Notes",
    },
}
BUG_FIELDS = ("version", "environment", "steps", "expected", "observed", "reproducibility", "notes")

CRITERIA = re.compile(r"^#+\s*(crit[eè]res? d['’]acceptation|acceptance criteria)\b", re.I | re.M)

ISSUE = """query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){issue(number:$n){
  id number title state url body labels(first:20){nodes{name}}
  projectItems(first:20){nodes{project{number} fieldValueByName(name:"Status"){
    ... on ProjectV2ItemFieldSingleSelectValue{name}}}}
  closedByPullRequestsReferences(first:10,includeClosedPrs:true){nodes{number title state merged url
    mergeCommit{oid} closingIssuesReferences(first:20){nodes{id number title state}}}}
  subIssues(first:50){nodes{id number title state}}
  parent{id number title state}}}}"""


# ---------------------------------------------------------------------------------------------------------------
# Pure helpers (tested)
# ---------------------------------------------------------------------------------------------------------------

def statuses(issue: dict, boards: dict) -> dict[str, str | None]:
    """Status of the ticket on each board (po/dev/qa), None when it is not on it. `boards`: [boards] of flow.toml."""
    numbers = {int(boards[k]): k for k in NAMES}
    out: dict[str, str | None] = {k: None for k in NAMES}
    for item in issue["projectItems"]["nodes"]:
        key = numbers.get(item["project"]["number"])
        if key:
            out[key] = (item["fieldValueByName"] or {}).get("name") or ""
    return out


def linked(issue: dict) -> list[dict]:
    """The tickets that go with this one: closed by the same pull requests, its sub-issues, its parent. Sorted, no
    duplicates, without the ticket itself."""
    found: dict[int, dict] = {}
    for pr in issue["closedByPullRequestsReferences"]["nodes"]:
        for other in pr["closingIssuesReferences"]["nodes"]:
            found[other["number"]] = other
    for other in issue["subIssues"]["nodes"]:
        found[other["number"]] = other
    if issue.get("parent"):
        found[issue["parent"]["number"]] = issue["parent"]
    found.pop(issue["number"], None)
    return [found[n] for n in sorted(found)]


def has_criteria(body: str) -> bool:
    """The ticket has an acceptance criteria section — what the PO validates against."""
    return bool(CRITERIA.search(body or ""))


def text(config: dict) -> dict:
    lang = config["project"].get("language", "fr")
    if lang not in TEXT:
        raise flowlib.FlowError(f"flow.toml [project] language « {lang} » : {' ou '.join(TEXT)}")
    return TEXT[lang]


def bug_body(tx: dict, fields: dict, breaks: int | None = None) -> str:
    """The bug ticket: one section per field given (version, environment, steps, expected, observed,
    reproducibility, notes), in that order, then the ticket it breaks."""
    missing = [f for f in ("steps", "expected", "observed", "reproducibility") if not (fields.get(f) or "").strip()]
    if missing:
        raise flowlib.FlowError(f"bug incomplet : {', '.join(missing)} — sans eux les devs ne peuvent pas le reproduire")
    parts = [f"## {tx[f]}\n\n{fields[f].strip()}" for f in BUG_FIELDS if (fields.get(f) or "").strip()]
    if breaks:
        parts.append(tx["breaks"].format(number=breaks))
    return "\n\n".join(parts) + "\n"


def merged_prs(issue: dict) -> list[dict]:
    return [pr for pr in issue["closedByPullRequestsReferences"]["nodes"] if pr["merged"]]


def validate_problems(issue: dict, status: dict) -> list[str]:
    """Why the PO cannot validate the ticket yet (empty: it can)."""
    problems = []
    if not merged_prs(issue):
        problems.append("aucune PR mergée ne ferme ce ticket : rien à valider sur la branche par défaut")
    if status.get("dev") not in (VALIDATED, DONE):
        problems.append(f"Dev « {status.get('dev') or 'absent'} » : le PO valide un ticket en {VALIDATED} (mergé)")
    return problems


# ---------------------------------------------------------------------------------------------------------------
# GitHub
# ---------------------------------------------------------------------------------------------------------------

class Tickets:
    def __init__(self, config: dict):
        self.config = config
        self.gh = boards_sync.client(config)
        self.owner, self.repo = config["project"]["repository"].split("/")
        self._boards: dict[str, tuple] = {}

    def issue(self, number: int) -> dict:
        issue = self.gh.gql(ISSUE, o=self.owner, r=self.repo, n=number)["repository"]["issue"]
        if not issue:
            raise flowlib.FlowError(f"#{number} n'existe pas dans {self.owner}/{self.repo} (ou c'est une PR)")
        return issue

    def board(self, key: str) -> tuple:
        if key not in self._boards:
            self._boards[key] = self.gh.board(int(self.config["boards"][key]))
        return self._boards[key]

    def set_status(self, key: str, issue_id: str, status: str) -> None:
        project, field, options, cards, _ = self.board(key)
        if status not in options:
            raise flowlib.FlowError(f"le board {NAMES[key]} n'a pas de colonne « {status} » ({', '.join(options)})")
        card = cards.get(issue_id)
        self.gh.apply(project, field, options[status], issue_id, card.item if card else None)

    def comment(self, issue_id: str, body: str) -> None:
        self.gh.gql("mutation($s:ID!,$b:String!){addComment(input:{subjectId:$s,body:$b}){clientMutationId}}",
                    s=issue_id, b=body)

    def reopen(self, issue_id: str) -> None:
        self.gh.gql("mutation($i:ID!){reopenIssue(input:{issueId:$i}){clientMutationId}}", i=issue_id)

    def close(self, issue_id: str) -> None:
        self.gh.gql("mutation($i:ID!){closeIssue(input:{issueId:$i,stateReason:COMPLETED}){clientMutationId}}",
                    i=issue_id)

    def create_issue(self, title: str, body: str, labels: list[str]) -> dict:
        req = urllib.request.Request(
            f"https://api.github.com/repos/{self.owner}/{self.repo}/issues",
            data=json.dumps({"title": title, "body": body, "labels": labels}).encode(),
            headers={"Authorization": f"bearer {self.gh.tok}", "Accept": "application/vnd.github+json"},
            method="POST",
        )
        with urllib.request.urlopen(req) as resp:
            return json.load(resp)


# ---------------------------------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------------------------------

def label(t: dict) -> str:
    return f"#{t['number']} {t['title']} ({t['state'].lower()})"


def cmd_show(t: Tickets, number: int) -> int:
    issue = t.issue(number)
    status = statuses(issue, t.config["boards"])
    print(f"#{issue['number']} {issue['title']} — {issue['state'].lower()}, {issue['url']}")
    labels = [x["name"] for x in issue["labels"]["nodes"]]
    print(f"labels : {', '.join(labels) or 'aucun'}")
    print("boards : " + " ; ".join(f"{NAMES[k]} = {v if v is not None else '—'}" for k, v in status.items()))
    print(f"critères d'acceptation : {'oui' if has_criteria(issue['body']) else 'NON (section « ## Critères d’acceptation » absente)'}")
    for pr in issue["closedByPullRequestsReferences"]["nodes"]:
        state = "mergée " + pr["mergeCommit"]["oid"][:7] if pr["merged"] else pr["state"].lower()
        print(f"PR #{pr['number']} {pr['title']} — {state}, {pr['url']}")
    for other in linked(issue):
        print(f"lié : {label(other)}")
    return 0


def cmd_list(t: Tickets, key: str, status: str | None) -> int:
    _, _, _, cards, names = t.board(key)
    rows = sorted(((c.status or "", names[i]) for i, c in cards.items() if status is None or c.status == status),
                  key=lambda r: (r[0], int(r[1].split("#")[1])))
    for st, name in rows:
        print(f"{st or 'sans statut':20} {name}")
    print(f"{len(rows)} carte(s) sur {NAMES[key]}{f' en {status}' if status else ''}.")
    return 0


def cmd_move(t: Tickets, number: int, key: str, status: str, comment: str | None, dry: bool) -> int:
    issue = t.issue(number)
    options = t.board(key)[2]
    if status not in options:
        raise flowlib.FlowError(f"le board {NAMES[key]} n'a pas de colonne « {status} » ({', '.join(options)})")
    print(f"{'[dry-run] ' if dry else ''}#{number} : {NAMES[key]} → {status}")
    if not dry:
        t.set_status(key, issue["id"], status)
        if comment:
            t.comment(issue["id"], comment)
    return 0


def cmd_validate(t: Tickets, number: int, comment: str | None, dry: bool) -> int:
    issue = t.issue(number)
    problems = validate_problems(issue, statuses(issue, t.config["boards"]))
    if problems:
        raise flowlib.FlowError("validation impossible — " + " ; ".join(problems))
    commits = ", ".join(pr["mergeCommit"]["oid"][:7] for pr in merged_prs(issue))
    body = f"{text(t.config)['validated'].format(commits=commits)}\n\n{comment or ''}".rstrip()
    print(f"{'[dry-run] ' if dry else ''}#{number} : Dev → {DONE} (part en QA à la prochaine release)")
    if not dry:
        t.set_status("dev", issue["id"], DONE)
        t.comment(issue["id"], body)
        if issue["state"] == "OPEN":
            t.close(issue["id"])
    return 0


def cmd_reopen(t: Tickets, number: int, reason: str, only: bool, dry: bool) -> int:
    issue = t.issue(number)
    targets = [issue] + ([] if only else linked(issue))
    tx = text(t.config)
    prs = ", ".join(f"#{pr['number']}" for pr in merged_prs(issue)) or tx["no_pr"]
    for target in targets:
        why = reason if target is issue else tx["linked"].format(number=number, reason=reason)
        state = "rouvert" if target["state"] != "OPEN" else "déjà ouvert"
        print(f"{'[dry-run] ' if dry else ''}#{target['number']} : {state}, Dev → {READY} — {why}")
        if dry:
            continue
        if target["state"] != "OPEN":
            t.reopen(target["id"])
        t.set_status("dev", target["id"], READY)
        t.comment(target["id"], f"{tx['refused'].format(prs=prs)}\n\n{why}")
    return 0


def cmd_bug(t: Tickets, title: str, fields: dict, for_number: int | None, blocker: bool, dry: bool) -> int:
    tx = text(t.config)
    labels = ["type:bug"] + (["release-blocker"] if blocker else [])
    broken = t.issue(for_number) if for_number else None
    body = bug_body(tx, fields, for_number)
    print(f"{'[dry-run] ' if dry else ''}bug « {title} » ({', '.join(labels)}) : QA → {BUG}, Dev → {READY}"
          + (f" ; #{for_number} : QA → {BUG}" if broken else ""))
    if dry:
        print(body)
        return 0
    created = t.create_issue(title, body, labels)
    t.set_status("qa", created["node_id"], BUG)
    t.set_status("dev", created["node_id"], READY)
    if broken:
        t.set_status("qa", broken["id"], BUG)
        t.comment(broken["id"], tx["bug_comment"].format(number=created["number"], title=title))
    print(f"créé : #{created['number']} {created['html_url']}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("show", help="un ticket, ses statuts, ses PR, ses tickets liés")
    p.add_argument("number", type=int)
    p = sub.add_parser("list", help="les cartes d'un board")
    p.add_argument("board", choices=list(NAMES))
    p.add_argument("--status")
    p = sub.add_parser("move", help="change le Status d'un ticket sur un board")
    p.add_argument("number", type=int)
    p.add_argument("board", choices=list(NAMES))
    p.add_argument("status")
    p.add_argument("--comment")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("validate", help="PO : fonctionnalité validée sur la branche par défaut → Dev Done")
    p.add_argument("number", type=int)
    p.add_argument("--comment")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("reopen", help="PO : refusée → ticket et tickets liés rouverts, Dev Ready to dev")
    p.add_argument("number", type=int)
    p.add_argument("--reason", required=True)
    p.add_argument("--only", action="store_true", help="sans les tickets liés")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("bug", help="QA : un bug trouvé, en ticket pour les devs")
    p.add_argument("--title", required=True)
    p.add_argument("--steps", required=True, help="les étapes exactes pour reproduire")
    p.add_argument("--expected", required=True)
    p.add_argument("--observed", required=True, help="ce qui se passe : sortie, message, état laissé")
    p.add_argument("--reproducibility", required=True,
                   help="à quelle fréquence et dans quelles conditions, ex. « 5/5 », « 2/10 sous charge »")
    p.add_argument("--version", help="la version testée, ex. 1.2.0-rc.3")
    p.add_argument("--environment", help="l'environnement de test (image, système, commande de création)")
    p.add_argument("--notes", help="journaux, pistes, captures")
    p.add_argument("--for", dest="for_number", type=int, help="le ticket que ce bug casse")
    p.add_argument("--blocker", action="store_true", help="bloque la release (label release-blocker)")
    p.add_argument("--dry-run", action="store_true")
    flowlib.utf8_stdout()  # before argparse: --help is French
    args = parser.parse_args(argv)
    try:
        t = Tickets(flowlib.load_config())
        if args.command == "show":
            return cmd_show(t, args.number)
        if args.command == "list":
            return cmd_list(t, args.board, args.status)
        if args.command == "move":
            return cmd_move(t, args.number, args.board, args.status, args.comment, args.dry_run)
        if args.command == "validate":
            return cmd_validate(t, args.number, args.comment, args.dry_run)
        if args.command == "reopen":
            return cmd_reopen(t, args.number, args.reason, args.only, args.dry_run)
        fields = {f: getattr(args, f) for f in BUG_FIELDS}
        return cmd_bug(t, args.title, fields, args.for_number, args.blocker, args.dry_run)
    except flowlib.FlowError as e:
        print(f"ticket : {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
