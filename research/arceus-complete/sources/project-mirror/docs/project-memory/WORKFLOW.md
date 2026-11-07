# Memory maintenance and handoff protocol

## During work
Keep one task per objective using the [task template](templates/task.md). Choose a unique date/topic filename, adding an agent/session suffix on collision. Task records are authoritative for their own scope; STATE is a small project-level view, not a second detailed journal. Review work uses a separate task, with findings and disagreements linked back to the original.

Before investigation, read relevant topic records and search INDEX/records for symbols, addresses and previous experiments. Reproduce only when evidence is missing, stale, contradictory or affected by a changed build; record why. Avoid routine per-turn entries. After a meaningful experiment/change, immediately update its record with the reproducible command/input, outcome, evidence, limits and exact next action. Do this before beginning a new expensive investigation. Save substantive blocked/unfinished work before responding to the user.

Promote reusable discoveries to one canonical topic record using the [finding template](templates/finding.md). Link from the task instead of copying the details. Capture symbol names/aliases, module/build, address space, offsets, signatures/bytes, callers/callees, layouts and relationships when relevant. Unknown fields stay unknown. Cite file/function plus revision/hash or captured run and artifact path; line numbers alone drift. Preserve the decisive argument, alternatives eliminated and limits, not hidden chain-of-thought or a full transcript.

## Status and evidence
- Findings: confirmed (direct evidence within stated scope), hypothesis (testable interpretation), refuted (contradicted under stated conditions), superseded (replaced by linked evidence). Confirmation is scoped: compilation, numerical fixture and user-tested visual behavior establish different things.
- Tasks: planned, ongoing, blocked, completed. Completed tasks include validation and remaining limitations. Blocked tasks name the missing dependency and a restart condition. No ownership is inferred from a tool/model name alone.
- Failed experiments keep setup/build/flags, actual result, why the approach failed or was insufficient, and what would justify retrying. An experiment can fail to solve the symptom while still finding a real defect.
- Disagreements stay visible with competing evidence and a discriminating test. Do not silently rewrite history; mark older claims superseded and link the replacement. User feedback is evidence with its stated test scope.

## Multiple agents
Read git status and relevant task ownership first. Prefer separate worktrees/patch directories for code and separate task records for documentation. Record owner/session, scope, changed paths and last checkpoint with timezone. Claims are advisory; inspect current activity before taking over stale work. Do not erase a claim on age alone. If concurrency cannot be verified, take independent read-only analysis and document it separately.

Each worker edits its own task/topic scope. The coordinating agent reconciles STATE and shared indexes after re-reading them, applying small changes that retain others' updates. Do not rewrite shared files from an old snapshot. If a file changed during editing, re-read and merge. Worktrees require committed/merged memory to reach another checkout; include memory in the same reviewed change as its implementation. Do not auto-commit, publish or contact agents as part of bookkeeping.

## Compact and resume
At milestones or when STATE exceeds 500 words, move durable detail into topic/task records and leave links. Keep completed tasks and evidence searchable; archive completed task groups by topic/year only when necessary, updating INDEX and links. Avoid an ever-growing chronological changelog. Search INDEX and relevant directories; never bulk-load all completed work. New topic indexes can shard a growing INDEX.

Recommend a fresh chat when irrelevant history dominates, reasoning/failed approaches repeat, evidence is being lost, or a major milestone/objective change provides a clean boundary. Save first: objective/status, file/branch/build identity, findings and hypotheses, failed attempts, changes, validation, unresolved risks, exact next action and any running process/test ownership. Verify links and actual saved content. Then state that the handoff is saved and give a short resume instruction. Never depend on the old chat remaining accessible. Do not recommend a reset mid-operation until live process state and safe handling are recorded.

At every substantive completion or blocker, ensure the task is resumable without further user paperwork. A sudden shutdown can lose unsaved observations; frequent meaningful checkpoints reduce this risk. Instruction files guide agents but are not a background recorder or guaranteed enforcement.

## Checks
From repository root: node tools/check-project-memory.cjs

The checker verifies required entry files, relative Markdown links, task metadata/status, and startup word budgets. It does not establish truth, detect all concurrent edits, or validate external local artifacts. Run it after changing memory structure or finishing a handoff. Read-only reviews need only record new findings and their scope; skip ceremonial updates when nothing meaningful changed.
