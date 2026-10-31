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
