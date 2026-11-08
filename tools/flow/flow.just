# Gestion de projet (plugin Claude Code project-flow) — importé par le justfile : import? 'tools/flow/flow.just'
# Méthode : CONTRIBUTING.md. Configuration : flow.toml.

flow_python := env("PYTHON", "python3")

[group('flow')]
[doc('Branche les hooks git du dépôt (.githooks : refuse un commit sans référence de ticket)')]
hooks:
    git config core.hooksPath .githooks

[group('flow')]
[doc('Passages entre les boards PO Refinement, Dev et QA : Approved → Ready to dev, Missing information → Refined ticket, Rejected → Rejected, Bug → Ready to dev, nouvelles issues → Proposition. "dry-run" les liste seulement')]
boards-sync mode="":
    {{ flow_python }} tools/flow/boards_sync.py {{ if mode == "dry-run" { "--dry-run" } else { "" } }}

[group('flow')]
[doc('La synchro, plus chaque ticket Dev Done → QA Release ready (lancé par just release). "dry-run" les liste seulement')]
boards-release mode="":
    {{ flow_python }} tools/flow/boards_sync.py --release {{ if mode == "dry-run" { "--dry-run" } else { "" } }}

[group('flow')]
[doc('Vérifie une référence de ticket : just ticket-check title "Titre de PR (#12)" ou just ticket-check range origin/main HEAD')]
[positional-arguments]
ticket-check +args:
    {{ flow_python }} tools/flow/ticket_check.py "$@"

[group('flow')]
[doc('Un ticket sur les trois boards (rôles PO, Dev, QA) : show <n>, list <po|dev|qa>, move <n> <board> "<Status>", validate <n>, reopen <n> --reason "…", bug --title … --steps … --expected … --observed … --reproducibility …')]
[positional-arguments]
ticket +args:
    {{ flow_python }} tools/flow/ticket.py "$@"

[group('release')]
[doc('Part en release : release/X.Y.0 depuis la branche par défaut (features figées) ou release/X.Y.Z depuis le dernier tag X.Y figé (correctif) ; Dev Done → QA. Chaque push construit une release candidate X.Y.Z-rc.N. Version déjà utilisée refusée. "dry-run" montre seulement')]
release version mode="":
    {{ flow_python }} tools/flow/release.py start {{ version }} {{ if mode == "dry-run" { "--dry-run" } else { "" } }}

[group('release')]
[doc('QA validée : tag vX.Y.Z sur release/X.Y.Z, qui construit le package de production. Refusé si un ticket QA n’est pas en Ready to deploy ou si le dernier commit n’a pas de release candidate. Options : --force, --dry-run')]
release-freeze version *options:
    {{ flow_python }} tools/flow/release.py freeze {{ version }} {{ options }}

# Recettes installées selon le projet (package project-flow, deploy SSH, base de données).
import? 'flow-package.just'
import? 'flow-deploy.just'
import? 'flow-db.just'
