# Persistent memory installation

Status: completed
Owner: Codex / memory-setup chat (2026-11-06)
Updated: 2026-11-06 America/Chicago
Scope: shared repository instructions, memory, structural checker and local entry-point bridges

## Objective and result
Enable fresh Codex and Claude chats to resume with a small common entry point, preserve evidence and failed approaches, and checkpoint automatically as part of development. Canonical records live in this Git checkout. Task-specific records separate ongoing work from completed baselines and future objectives.

## Changes and evidence
Added root AGENTS.md and CLAUDE.md importing AGENTS, docs/project-memory/ entry/state/workflow/environment/index, scoped findings, active lighting task, templates, and tools/check-project-memory.cjs. Research workspace and non-Git runtime source have short AGENTS/CLAUDE pointers to canonical memory. Existing chronological workspace instructions are preserved byte-for-byte in ../archive/2026-11-06-legacy-instructions.txt (relative to this task directory); SHA256 f6aea42c6cd84c4e94f4e1c7a5a385256923718ae1912a2c072bb89435494ce8.

Added root-only .gitignore exceptions for AGENTS.md and CLAUDE.md; the existing ignore rules otherwise hid these essential entry files from version control. No application code, game assets, binaries, settings or existing staged changes were modified. No commit, launch, agent messaging, or publication. Legacy reports indexed selectively; memory import is not independent verification of their technical claims. Local path bridges are intentionally machine-specific; canonical content and templates are repository-local.

## Validation
node tools/check-project-memory.cjs passed: required entry files, local Markdown links, task metadata/status, startup/state word budgets. Resume walkthrough verified: workspace instructions -> repository AGENTS -> START -> STATE -> lighting task -> scoped rendering findings -> original local report/capture. Installed EXE independently hashed to B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019; no ArceusTest process found. Legacy snapshot hash matches the pre-replacement bytes. Source checkout is already dirty; all pre-existing changes retained. Full git diff --check reports a pre-existing blank line at arceus_clock.c:939; memory setup does not modify it. The changed .gitignore passes its scoped diff check.

## Next action and handoff

Final structural check: 998 startup words (316 STATE words), 13 Markdown files. A copied fixture accepted the valid memory and rejected broken links, invalid task status and oversized startup. Key external report/capture/candidate paths exist. These fixture checks ran in the memory-setup chat's scratch directory, without mutating canonical records.
Default technical objective is the lighting-box task. Existing other chats will not necessarily reload newly created instructions mid-session; read the changed instruction file when continuing them. Future chats must open this repository, the research workspace, or the mapped runtime source. A projectless chat elsewhere cannot discover this memory automatically.

Agent-driven checkpoints are instructions, not a background recorder or a hard guarantee. Any unsaved work at abrupt shutdown can be lost. Cross-machine/cross-worktree reuse requires carrying the repository memory and separately retaining local evidence. User is not expected to maintain the records manually.
