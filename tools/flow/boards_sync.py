"""Hand-offs between the three boards (PO Refinement, Dev, QA) — just boards-sync, or Actions → Boards sync.

One ticket lives on up to three GitHub Projects, each with its own Status. GitHub moves a card only inside its
board, so this script carries the hand-offs between boards:

    PO  Approved             → Dev Ready to dev       (added to Dev, or back from Missing information)
    Dev Missing information  → PO  Refined ticket     (the PO completes the ticket, then approves it again)
    Dev Rejected             → PO  Rejected
    Dev Done                 → QA  Release ready      only with --release: a release is started by hand
    QA  Bug                  → Dev Ready to dev
    open issue of the repository on no board → PO Proposition

When both sides changed, the last Status change wins (updatedAt of the field value), so a run is idempotent and a
ticket sent back never bounces. Blocked, Reviewed, Validated… stay inside their board.

    python3 tools/flow/boards_sync.py [--release] [--dry-run]

Configuration: [project] repository and [boards] owner, po, dev, qa of flow.toml.
Token: GH_TOKEN or GITHUB_TOKEN (scopes `project` and `repo`), else `gh auth token`. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flowlib  # noqa: E402

NAMES = {"po": "PO Refinement", "dev": "Dev", "qa": "QA"}

PROPOSITION, REFINED, APPROVED, REJECTED = "Proposition", "Refined ticket", "Approved", "Rejected"
READY, MISSING, DONE = "Ready to dev", "Missing information", "Done"
RELEASE_READY, BUG = "Release ready", "Bug"


@dataclass(frozen=True)
class Card:
    item: str  # project item id
    status: str | None
    updated: str  # ISO 8601 (UTC, Z) of the last Status change: comparable as a string


@dataclass(frozen=True)
class Action:
    board: str  # po | dev | qa
    issue: str  # issue node id
    status: str
    item: str | None = None  # None: add the issue to the board first
    why: str = ""


def newer(a: Card, b: Card | None) -> bool:
    return b is None or a.updated > b.updated


def plan(boards: dict[str, dict[str, Card]], open_issues: list[str], release: bool = False) -> list[Action]:
    """The moves to make, from the cards of each board (by issue id) and the open issues of the repository.
    `release`: also send the Dev Done tickets to QA (a release is started by hand, never by a plain sync)."""
    actions: list[Action] = []

    def move(board: str, issue: str, status: str, why: str) -> None:
        card = boards[board].get(issue)
        if card is None or card.status != status:
            actions.append(Action(board, issue, status, card.item if card else None, why))

    on_board = set().union(*(set(cards) for cards in boards.values()))
    for issue in open_issues:
        if issue not in on_board:
            move("po", issue, PROPOSITION, "nouvelle issue")

    for issue in sorted(on_board):
        po, dev, qa = (boards[b].get(issue) for b in ("po", "dev", "qa"))
        if po and po.status == APPROVED and (dev is None or (dev.status == MISSING and newer(po, dev))):
            move("dev", issue, READY, "PO Approved")
        if dev and dev.status == MISSING and newer(dev, po):
            move("po", issue, REFINED, "Dev Missing information")
        if dev and dev.status == REJECTED and po and newer(dev, po):
            move("po", issue, REJECTED, "Dev Rejected")
        if release and dev and dev.status == DONE and (qa is None or (qa.status == BUG and newer(dev, qa))):
            move("qa", issue, RELEASE_READY, "Dev Done")
        if qa and qa.status == BUG and newer(qa, dev) and (dev is None or dev.status == DONE):
            move("dev", issue, READY, "QA Bug")
    return actions


# ---------------------------------------------------------------------------------------------------------------
# GitHub
# ---------------------------------------------------------------------------------------------------------------

class GitHub:
    def __init__(self, tok: str, owner: str):
        self.tok = tok
        self.owner = owner

    def gql(self, query: str, **variables) -> dict:
        req = urllib.request.Request(
            "https://api.github.com/graphql",
            data=json.dumps({"query": query, "variables": variables}).encode(),
            headers={"Authorization": f"bearer {self.tok}", "Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req) as resp:
            body = json.load(resp)
        if body.get("errors"):
            raise flowlib.FlowError(json.dumps(body["errors"], ensure_ascii=False))
        return body["data"]

    def board(self, number: int) -> tuple[str, str, dict[str, str], dict[str, Card], dict[str, str]]:
        """Project id, Status field id, option ids by name, cards by issue id, issue labels (repo#n) by id.
        The owner is a user or an organization: both are a ProjectV2Owner."""
        cards: dict[str, Card] = {}
        labels: dict[str, str] = {}
        after = None
        while True:
            owner = self.gql(
                """query($o:String!,$n:Int!,$a:String){repositoryOwner(login:$o){... on ProjectV2Owner{projectV2(number:$n){
                     id field(name:"Status"){... on ProjectV2SingleSelectField{id options{id name}}}
                     items(first:100,after:$a){pageInfo{hasNextPage endCursor} nodes{id
                       fieldValueByName(name:"Status"){... on ProjectV2ItemFieldSingleSelectValue{name updatedAt}}
                       content{... on Issue{id number repository{name}}}}}}}}}""",
                o=self.owner, n=number, a=after,
            )["repositoryOwner"]
            p = (owner or {}).get("projectV2")
            if not p:
                raise flowlib.FlowError(f"le Project {self.owner} n°{number} est introuvable (flow.toml [boards])")
            for node in p["items"]["nodes"]:
                issue = node["content"] or {}
                if "id" not in issue:
                    continue  # draft or pull request
                value = node["fieldValueByName"] or {}
                cards[issue["id"]] = Card(node["id"], value.get("name"), value.get("updatedAt", ""))
                labels[issue["id"]] = f"{issue['repository']['name']}#{issue['number']}"
            if not p["items"]["pageInfo"]["hasNextPage"]:
                options = {o["name"]: o["id"] for o in p["field"]["options"]}
                return p["id"], p["field"]["id"], options, cards, labels
            after = p["items"]["pageInfo"]["endCursor"]

    def open_issues(self, repository: str) -> dict[str, str]:
        owner, name = repository.split("/")
        issues: dict[str, str] = {}
        after = None
        while True:
            r = self.gql(
                """query($o:String!,$r:String!,$a:String){repository(owner:$o,name:$r){
                     issues(states:OPEN,first:100,after:$a){pageInfo{hasNextPage endCursor} nodes{id number}}}}""",
                o=owner, r=name, a=after,
            )["repository"]["issues"]
            issues.update({n["id"]: f"{name}#{n['number']}" for n in r["nodes"]})
            if not r["pageInfo"]["hasNextPage"]:
                return issues
            after = r["pageInfo"]["endCursor"]

    def apply(self, project: str, field: str, option: str, issue: str, item: str | None) -> None:
        if item is None:
            item = self.gql(
                "mutation($p:ID!,$c:ID!){addProjectV2ItemById(input:{projectId:$p,contentId:$c}){item{id}}}",
                p=project, c=issue,
            )["addProjectV2ItemById"]["item"]["id"]
        self.gql(
            """mutation($p:ID!,$i:ID!,$f:ID!,$o:String!){updateProjectV2ItemFieldValue(input:{
                 projectId:$p,itemId:$i,fieldId:$f,value:{singleSelectOptionId:$o}}){clientMutationId}}""",
            p=project, i=item, f=field, o=option,
        )


def client(config: dict) -> GitHub:
    boards = config["boards"]
    if not all(boards.get(k) for k in ("owner", "po", "dev", "qa")):
        raise flowlib.FlowError("flow.toml [boards] incomplet : owner, po, dev, qa (lancez /project-init)")
    return GitHub(flowlib.token(), boards["owner"])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dry-run", action="store_true", help="print the moves without making them")
    parser.add_argument("--release", action="store_true", help="start a release: Dev Done → QA Release ready")
    flowlib.utf8_stdout()  # before argparse: --help is French
    args = parser.parse_args(argv)
    try:
        config = flowlib.load_config()
        gh = client(config)
        meta, boards, labels = {}, {}, {}
        for key in NAMES:
            project, field, options, cards, names = gh.board(int(config["boards"][key]))
            meta[key] = (project, field, options)
            boards[key] = cards
            labels.update(names)
        issues = gh.open_issues(config["project"]["repository"])
        labels.update(issues)

        actions = plan(boards, list(issues), release=args.release)
        for a in actions:
            verb = "ajout" if a.item is None else "statut"
            print(f"{labels.get(a.issue, a.issue)}: {NAMES[a.board]} → {a.status} ({verb}, {a.why})")
            if not args.dry_run:
                project, field, options = meta[a.board]
                if a.status not in options:
                    raise flowlib.FlowError(f"le board {NAMES[a.board]} n'a pas de colonne « {a.status} »")
                gh.apply(project, field, options[a.status], a.issue, a.item)
    except flowlib.FlowError as e:
        print(f"boards-sync : {e}", file=sys.stderr)
        return 1
    print(f"{len(actions)} mouvement(s){' à faire (dry-run)' if args.dry_run else ''}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
