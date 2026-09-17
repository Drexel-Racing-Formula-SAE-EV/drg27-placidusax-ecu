# Repository conventions

## Commit attribution

All commits in this repository are authored by the repository owner:

```text
Logan Sauerwald <lss69@drexel.edu>
```

Set both author and committer to that identity:

```text
git config user.name  "Logan Sauerwald"
git config user.email "lss69@drexel.edu"
```

Do **not** add AI-assistant attribution to anything that lands in this
repository. Specifically, no `Co-Authored-By:` trailer naming an AI assistant,
no session-link trailer, and no assistant name or model identifier in commit
messages, pull request titles or bodies, code comments, or documentation.

This applies to every commit regardless of how the change was produced.

## Migration context

This repository is the Zephyr RTOS port of the DER26 vehicle ECU firmware.
The behavioral oracle is `DER26-ECU-v2.10.7-SAFETY2-20260827`; Zephyr replaces
platform mechanisms and does not redefine ECU safety behavior.

Start with `docs/migration/E000_MIGRATION_PLAN.md`. Each stage lands as one
reviewable commit with a closeout document under `docs/migration/`.

Before committing, run:

```text
python3 scripts/check_all_contracts.py . [build-dir]
```
