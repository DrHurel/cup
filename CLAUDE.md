# CLAUDE.md

## Commit messages

Every commit subject uses exactly this format:

```
[<issues-tag>] feat(<scope>) : <message>
[<issues-tag>] refactor(<scope>) : <message>
[<issues-tag>] fix(<scope>) : <message>
```

- `<issues-tag>`: the issue(s) the commit addresses — a GitHub issue (`#42`) or
  a SonarCloud rule key (`cpp:S5272`); several are comma-separated
  (`[#42, cpp:S5350]`). When the commit addresses no issue, the tag is
  `[no-issue]`.
- Type: only `feat`, `refactor` or `fix`.
- `<scope>`: the library or area touched (`ui`, `docker`, `project`, `ci`, ...).
- Note the spaces around the colon: `fix(ui) : ...`, not `fix(ui): ...`.

Example: `[cpp:S5272] fix(ui) : read the prompt answer without expected::transform`

The `commit-msg` hook (`just hooks`) and the *Ticket reference* workflow check
the tag (`[tickets]` in `flow.toml`). A PR title follows the same format: it
becomes the squash commit's subject.

<!-- project-flow:begin — section gérée par /project-init (plugin project-flow) -->
## Gestion de projet

Méthode complète : [CONTRIBUTING.md](CONTRIBUTING.md). Configuration : `flow.toml`. Outillage : `tools/flow/`.

- **Commits** : chaque commit référence son ou ses tickets (`#42` ou `owner/repo#42` ; motif : `[tickets]` de
  `flow.toml`). Une PR qui couvre plusieurs tickets = un commit par ticket. Le titre de PR porte la référence (il
  devient le message du squash).
- **Branches** : toujours rebasées sur la branche cible (`git fetch && git rebase origin/<cible>`), jamais de merge
  de la cible. Merge en squash de préférence, en rebase pour une PR multi-tickets.
- **Boards** : `just boards-sync [dry-run]` (`/boards-sync`). Dev Done → QA seulement via `just release`. Dev
  *Reviewed* = PR en revue, *Validated* = mergée, *Done* = validée par le PO. Un ticket : `just ticket show <n>`.
- **Rôles** : `/project-flow:po` (fonctionnel, validation après merge), `/project-flow:dev` (conception,
  implémentation), `/project-flow:qa` (recette hors CI). Chaque rôle reste dans son périmètre.
- **Releases** : `/release X.Y.Z`, `/release-freeze X.Y.Z`. Toujours `dry-run` d'abord, puis accord explicite de
  l'utilisateur.
- Chaque bug corrigé arrive avec son test.
<!-- project-flow:end -->
