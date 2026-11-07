const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const repo = 'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main';
const workspace = 'C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett';
const runtime = 'C:/Users/brett/Documents/Codex/work/nvn-src';
const mem = repo + '/docs/project-memory';
const files = {};
files['AGENTS.md'] = `# Arceus project instructions

## Start and resume
Read docs/project-memory/START.md, then STATE.md and only the selected task's linked records. If the user says "continue", follow STATE's next step after checking actual files and runtime state. Do not load the whole history. A new explicit objective takes precedence over the saved next step.

## Maintain memory as part of work
Checkpoint meaningful findings, ruled-out experiments, changed code, validation results and decisions as they happen, before switching objectives, before a user-facing completion/blocker, and before context compaction or a fresh-chat recommendation. No user reminder is needed. Save conclusions and the evidence/reason needed to reproduce them, not private scratch reasoning or transcripts. An abrupt interruption can only recover the last saved checkpoint.

Use docs/project-memory/WORKFLOW.md when creating/updating records or coordinating work. Findings have explicit confirmed/hypothesis/refuted/superseded status; tasks distinguish planned/ongoing/blocked/completed. Never promote an inference because it was repeated. Preserve failed approaches with conditions and a retry criterion. Update the task first; update the small STATE summary only when the project-level state changes. Memory updates belong with the relevant code change; do not auto-commit or publish.

When reasoning repeats, evidence becomes hard to retrieve, details are being lost, or a milestone/objective change makes history mostly irrelevant, save and verify a handoff, then recommend a fresh chat. Do not invent context/token percentages. No transcript dump or per-turn journal.

## Shared work and source safety
Read task ownership and git status before edits. Each agent owns a separate task record and isolated patch/worktree where feasible. Do not overwrite another agent's work, edit their record, reset the dirty checkout, or change installed runtime during a test. Reviewers record independent evidence before reconciliation. Re-read shared files immediately before small edits; preserve concurrent changes. A claim file is advisory, not an atomic lock. Do not spawn agents or message other chats unless authorized.

## Persistent user constraints
Keep game files, extracted modules, saves, raw logs, captures and research fixtures local. Do not upload them. Keep Vulkan --nvn_native=0, target 120 FPS, camera multiplier 2.2 and accepted menu/timing/blink/effects fixes. Target FPS is not measured displayed FPS. Preserve the tested B32D40B0 shadow baseline; see STATE for exact provenance.

Every game test must support the user's controller. Before launching, read the research workspace's outputs/controller-test-launch-notes.txt and the computer-use skill. Use the established native desktop helper/desktop launch route; restricted-shell launches failed controller enumeration. Check windows and a new log before retrying a helper with no visible window. The user saves and closes the game; do not kill, time-limit, restart, hot-patch, send gameplay input, or make Escape quit. Do not launch another test automatically after closure. Verify live run/install state; saved state is historical.

Runtime source is a separately modified non-Git working tree. The repository ScarletNX copy is a manually maintained mirror. Never assume source, mirror, build, staged EXE and installed EXE agree; record paths and hashes before replacement. Do not overwrite accepted binaries with an older staged candidate. Offline findings do not prove visual correctness, and image allocation/UI sharpness does not prove 3D viewport resolution.
`;
files['CLAUDE.md'] = `# Shared Arceus instructions\n\n@AGENTS.md\n\nUse the same repository memory as Codex. Keep Claude review findings in a separate task record; reconcile disagreements using evidence. Do not use private auto-memory as the only copy of project knowledge.\n`;
files['docs/project-memory/START.md'] = `# Project memory entry point

1. Read [STATE.md](STATE.md) (current objective, completed baseline, next action).
2. Read that task's record and linked findings only. Use [INDEX.md](INDEX.md) or targeted rg searches for a different objective. Never recursively read this directory or the legacy outputs.
3. Check git status/branch and relevant file hashes. Saved paths, ownership and runtime state can be stale. Preserve unrelated work. Continue the recorded next action when the user gives no newer objective.

[WORKFLOW.md](WORKFLOW.md) defines updates, evidence and handoffs. Read it when writing records or coordinating agents. [ENVIRONMENT.md](ENVIRONMENT.md) maps this machine and the source/mirror split. [Templates](templates/task.md) are for new records, not startup reading.

Default resume: open this repository or its existing research workspace and say "Continue the project." No manual handoff prompt or journal maintenance is expected. The assistant maintains these files. A chat opened elsewhere needs the project path; memory is not global. Unsaved discoveries cannot survive a crash/interruption.

Keep AGENTS + START + STATE under 1,500 words combined. Keep STATE under 500 words. Detailed evidence belongs in topic/task files and is read on demand. These are size targets, not exact token measurements.
`;
files['docs/project-memory/STATE.md'] = `# Current project state

Updated: 2026-11-06 (America/Chicago). Imported from existing local reports; installed runtime not rehashed during memory setup.

## Current objective and next action
Investigate purple/pink crafting-box highlights while preserving the successful native-shadow fix. Resume [lighting task](tasks/2026-11-06-lighting-box.md): isolate the box lighting shader/bindings in existing capture 181800, then trace normal/material inputs and specular/environment contributions. No new game launch is needed to start. Root cause is unproven; it is not established as a regression or unintended lighting.

## Completed and accepted
- Shadow test timing-20261106-181617-047 closed normally (exit 0). User reports original stone/bridge/wheel pattern resolved; captured finals support this for tested scenes. See [shadow findings](findings/shadows.md).
- Tested runtime SHA256: B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019. Preserve it. The separately staged AAAD4627 runtime predates this accepted result and must not replace it casually.
- Accepted Vulkan configuration: target 120 FPS, camera 2.2, timing/menu/blink/effects corrections and controller support. 2560x1440 depth-tested scene viewports were observed; sustained displayed 120 FPS was not measured.

## Ongoing and unresolved
- Pink appears in scene lighting, absent in base color; material/specular/environment interpretation is a hypothesis. See task for capture locations and evidence.
- Broad timing/physics ownership and cadence remain incompletely established; see [timing findings](findings/timing.md). Do not apply blanket shared-solver multipliers.
- Claude had an independent shadow review in progress in prior notes. Its completion/ownership is unverified. Check before overlapping it; do not assume the old review is still running.

## Future objectives
After causal rendering corrections: eventual TAA, temporal FSR/DLSS, then exploratory ray/path tracing. These interests do not authorize wholesale rendering replacement. No unscheduled game test.

## Resume checks
Read [environment](ENVIRONMENT.md) before building. Checkout was dirty on branch experiment/arceus-clock-hooks at base 59c7e1780e50c74a8f92a53dcd77d017fd00cbf6 during setup. Runtime build source is outside Git; repository is its manually updated mirror. Latest reported test is closed, but check live state before touching binaries. Existing research has been indexed, not exhaustively revalidated or migrated.
`;
files['docs/project-memory/ENVIRONMENT.md'] = `# Local environment and provenance

Recorded 2026-11-06; verify paths and contents before use. These are local pointers, not portable dependencies. For a new machine, update this map; keep canonical knowledge in this repository.

| Role | Location |
| --- | --- |
| Git checkout / canonical memory | ${repo} |
| Research workspace, scripts, reports | ${workspace} |
| Actual runtime build source (non-Git) | ${runtime} |
| Repository source mirror | third_party/ScarletNX/ |
| Runtime build | C:/Users/brett/Documents/Codex/work/nvn-build |
| Installed game | C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest |
| Extracted game/update | C:/Users/brett/Documents/Codex/work/arceus-dump |
| Test runs | C:/Users/brett/Documents/Codex/work/arceus-runs |

Git setup observation: HEAD 59c7e1780e50c74a8f92a53dcd77d017fd00cbf6, branch experiment/arceus-clock-hooks, staged and unstaged modifications already present. Do not reset/rebase/clean them as setup. Existing docs/arceus-*.txt are retained. No build command was independently validated during memory installation; recover exact invocations from the relevant experiment's scripts/link-arguments.json and build notes rather than inventing one.

Arceus 1.1.1 main build prefix reported in early timing investigation: AEE8F150DDA1B5A8; cached main base 0x08005000. Re-check full module identity and bytes before patches. main+ offsets are not runtime virtual addresses. Captured GPU addresses are capture-specific and may be reused.

Private tested shadow candidate: research workspace work/native-shadow-candidate/ArceusNativeShadow-runtime.exe, SHA256 B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019. Do not infer that the shared source/mirror contains its private changes. Inspect candidate inputs and hashes. Launcher: outputs/Arceus-Shadow-Controller-Test.exe; read launch notes before any authorized test.

This checkout is the durable documentation home, but old evidence files/captures remain elsewhere on this machine. They are not backed up by committing the memory. Retain local artifacts and include memory with normal repository version control when committing authorized work. No game data is copied into Git by this system.
`;
files['docs/project-memory/WORKFLOW.md'] = `# Memory maintenance and handoff protocol

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
`;
files['docs/project-memory/INDEX.md'] = `# Knowledge index (load by topic)

Paths below under outputs/ and work/ refer to the research workspace in ENVIRONMENT, unless stated otherwise. Legacy reports are dated observations, not current instructions. Later scoped results can supersede earlier conclusions. Search with rg before reopening a prior line of investigation.

| Topic | Canonical record | Additional evidence (on demand) |
| --- | --- | --- |
| Shadow scaling; accepted B32 baseline | [shadows](findings/shadows.md) | outputs/arceus-shadow-test-results.txt; arceus-shadow-resampling-evidence.json; arceus-native-shadow-candidate.txt |
| Purple crafting-box lighting; next objective | [task](tasks/2026-11-06-lighting-box.md) | outputs/arceus-shadow-test-results.txt; work/shadow-test-review/ |
| Timing clocks, physics, addresses | [timing](findings/timing.md) | outputs/arceus-engine-research-pass2.txt through pass6.txt; arceus-physics-research-pass6.txt; arceus-fixed-step-audit.txt |
| Half-resolution screen fetch; BC comparison | [shadows experiments](findings/shadows.md) | outputs/arceus-material-investigation.txt; arceus-1440p-lighting-fix.txt; arceus-material-diagnostic-provenance.json |
| AI, timers, dialogue, events | [timing evidence map](findings/timing.md) | outputs/arceus-ai-*.txt; arceus-dialogue-*.txt; arceus-event-timers-pass5.txt; arceus-attention-timers-pass4.txt |
| Controller launch and test policy | root AGENTS; [environment](ENVIRONMENT.md) | outputs/controller-test-launch-notes.txt; arceus-controller-test-pass6.json |
| Memory-system setup | [completed task](tasks/2026-11-06-memory-system.md) | [previous instruction snapshot](archive/2026-11-06-legacy-instructions.txt) |

Older outputs/findings.txt describes the early presentation-only investigation; its "no normal-speed 60 FPS implementation" statement is historical, superseded by later accepted timing experiments. outputs/Claude-Arceus-Handoff.txt predates the successful shadow test; retain its evidence paths but do not follow its old current-state/launch statements. Earlier pass10 reports did not fix the complete stone/wood symptom.
`;
files['docs/project-memory/findings/shadows.md'] = `# Rendering: shadow scaling and lighting

Updated: 2026-11-06. Evidence imported from existing local reports, not newly reproduced during memory setup. Paths relative to research workspace in ENVIRONMENT.

## SHADOW-001 — confirmed within captured scenes
Arceus 1.1.1; Vulkan; global 4/3 scene scaling. Prior D32 shadow arrays sampled at 1024 square exactly matched nearest reduction from 1365-square producers (2,097,152 compared samples). Pattern was visible in receiver 540a88000 before main scene lighting. Evidence: outputs/arceus-shadow-buffer-investigation.txt and arceus-shadow-resampling-evidence.json.

Private candidate SNX_ARCEUS_NATIVE_SHADOWS=1 keeps paired 1024x1024 D32 and R8_UINT tag producers native, with matching pass-local viewport/scissor/constants and copies, while scene color/depth remain 2560x1440. Candidate predicate uses guest dimensions and format keys 1000a/f6; semantic identification of every future pass is not proven. See outputs/arceus-native-shadow-candidate.txt for private source/link inputs and limitations.

Test timing-20261106-181617-047, runtime SHA256 B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019: user confirmed stone/bridge/wheel pattern fixed; captured finals agree. Four snapshots 181707/181736/181740/181800 contain paired native shadows; 16 full-layer depth/tag comparisons, 41,943,040 bytes, zero mismatches. Normal closure exit 0. Evidence: outputs/arceus-shadow-test-results.txt, work/shadow-test-review/copy-verification.json, work/review-shadow-test.py. This proves the tested correction/scope, not all-scene correctness or sustained 120 FPS. Do not revert baseline to investigate remaining highlights.

## LIGHT-001 — confirmed coordinate defect; insufficient symptom fix
Pass10 found an exact FMUL.FTZ-by-2 screen-position chain quantized/rescaled incorrectly for packed-normal fetches at 4/3 scale. direct_flow=3 correction preserves physical coordinate mapping. Reported checks: 52 regressions; 30,774 coordinate assertions; 931 shipped fragments; 334 captured+CB replay shaders. Six shipped lighting/probe shaders changed; disabled output identical. Evidence: outputs/arceus-material-investigation.txt and arceus-1440p-lighting-fix.txt. Compilation/expression checks are not full GPU visual proof. Later run 175234-874 still showed the stone pattern: this correction alone was insufficient. Retry only with a distinct mapped shader/input discrepancy, not as the already-failed complete explanation.

## EXP-BC-001 — failed as a sufficient explanation
Run 172820-124 switched native_bc=0; 245 observed BC5 bindings used conversion 22/RG16. User still saw bumps that changed with movement. Evidence: outputs/arceus-material-investigation.txt. Observed BC5 mip/upload/sampler checks passed within reported fixtures. This rules out that decoder switch as a sufficient fix, not all possible texture defects. Retry only with a new texture-specific mismatch.

## LIGHT-BOX-001 — observation confirmed; cause hypothesis
Capture 181800 shows pink highlights in scene lighting 5508c0000, absent in base color 552a80000; scratches/edges align with normal detail 552210000. Postprocessing is not their first appearance. Material/specular/environment/probe contribution is a working hypothesis. No matched original-game reference or pre-shadow box capture establishes whether it is unintended or a new regression. See [active task](../tasks/2026-11-06-lighting-box.md). GPU addresses apply to these captures only.
`;
files['docs/project-memory/findings/timing.md'] = `# Timing and engine evidence map

Updated: 2026-11-06. Imported from reports; not re-run during setup. Detailed legacy evidence remains local in the research workspace outputs/; inspect only the relevant report before implementing.

## TIME-001 — confirmed static observations, build-scoped
Early outputs/findings.txt reports Arceus 1.1.1 main build prefix AEE8F150DDA1B5A8 and cached base 0x08005000. Offsets below are main-relative, not runtime VAs. Revalidate full build identity/bytes before using them.
- main+0x0397E8D0: five 64-bit durations 16,666,667 / 33,333,334 / 50,000,000 / 66,666,667 / 83,333,334 ns.
- main+0x02783048: signed W2 table index, duration into X2, virtual tail dispatch at vtable+0x10.
- constructor main+0x0277BD74: four initialized duration fields at object+0x80/+0x90/+0xA0/+0xB0; stores object pointer at main+0x042EC1F8.
- main+0x0277BEF4: writes indexed 16-byte record; main+0x0278303C: indexed record accessor. Near main+0x027814B4 duration converts to seconds; full consumer identity/reach was then unresolved.
These observations identify relationships, not semantic names for all functions. Earlier docs/arceus-timing.txt and docs/arceus-fixed-step-audit.txt preserve implementation context. Z-A addresses are not Arceus addresses.

## TIME-002 — accepted user test, limited coverage
outputs/arceus-engine-research-pass6.txt and arceus-controller-test-pass6.json report run 163108-125 with DualSense Edge+sensors, target120, camera2.2 and accepted menu/timing/blink/effects/event-camera corrections. User said camera/menu felt good. This run was not a 1440p test. Target setting/wrapper cadence does not prove sustained displayed FPS or correctness of every subsystem. Later shadow run supplies rendering baseline.

## PHYS-001 — confirmed distinct policies; ownership unresolved
Pass6 joins by exact (count,ns,world), not adjacent log lines. Main caller main+0x2AF2AB8: 69 samples, one fixed 1/30s world, four substeps, multi. main+0x2CD701C: 234 samples, 15 world addresses, supplied variable dt, two substeps, single. Pointer reuse occurred; an address is not a lifetime identity. Shared global sample counts cannot establish each world's solve rate.

Elapsed channel1 preparation includes transform-difference / dt and support checks; other worlds use supplied dt directly. A shared-solver multiplier would change already-variable paths. World+0x5B8 is a 32-bit postsolve serial established by construction/solve implementations. Pass6 added a guarded sampled read, but the report says it was absent from that completed capture. Inspect staged/current source and later captures before claiming live observation. Evidence: outputs/arceus-physics-research-pass6.txt and arceus-engine-research-pass6.txt. Exact gameplay ownership remains unresolved.

## Further evidence to load only as needed
outputs/arceus-ai-research-pass4.txt; arceus-ai-scheduler-pass5.txt; arceus-ai-turn-research-pass6.txt; arceus-attention-timers-pass4.txt; arceus-field-timers-pass4.txt; arceus-dialogue-research-pass5.txt and pass6.txt; arceus-event-timers-pass5.txt; arceus-physics-scheduler-pass5.txt; arceus-resolution-research-pass6.txt; arceus-research-provenance-pass6.json.

These reports are indexed, not all converted into confirmed findings. On first use, extract the useful scoped conclusions and failed approaches into a canonical topic record with exact evidence. Do not blindly rerun prior research or treat an old "next test" paragraph as current authorization.
`;
files['docs/project-memory/tasks/2026-11-06-lighting-box.md'] = `# Purple crafting-box highlights

Status: ongoing
Owner: unassigned (resume candidate; imported from previous Codex reports)
Updated: 2026-11-06 America/Chicago
Scope: offline lighting/material investigation; preserve tested shadow correction

## Objective and success condition
Identify the shader/input contribution producing the box's pink/purple scratches and edges; establish whether incorrect against a matched reference or demonstrable semantic defect before proposing a narrow correction. Do not call it a proven regression.

## Checkpoint and evidence
Latest reported run 181617-047 is closed exit0; verify actual process state. [Rendering findings](../findings/shadows.md) holds confirmed observations and prior insufficient fixes. Source/mirror/build/install differ; [environment](../ENVIRONMENT.md) maps them. No code was changed by this task during memory setup.

Research workspace evidence: outputs/arceus-shadow-test-results.txt. Full capture screenshot_20261106_181800_buffers_4; ordinary 181810 is a second view. Decoded intermediates under work/pass10-captures/screenshot_20261106_181800_buffers_4_attachments/: 42_551130000_layer0.png; 43_5519a0000_layer0.png (material data, not display color); 44_552210000_layer0-decoded-normal.png; 48_5508c0000_layer0.png. Final/basecolor/receiver previews in work/shadow-test-review/. Original raw buffers remain in game captures. work/decode-surface-capture.py supports --index; normal interpretation requires confirmed address/output slot. Do not treat all attachments with a recent last-write present as fully rewritten for this cutscene.

## Confirmed / hypotheses / failed approaches
Pink absent in brown/gold basecolor 552a80000; present in scene lighting 5508c0000 before final postprocessing; aligned with normal scratches. Hypothesis: material/specular/environment/probe lighting or input interpretation. Exact shader and cause unknown. Earlier receiver can retain stale other-scene data. BC decoder switch and pass10 fetch correction did not resolve the original stone symptom; shadow correction subsequently did. They are not fresh leads without new evidence.

## Next action
Inspect existing 181800 attachment metadata and draw/material traces to identify the box lighting shader and actual bindings. Follow normal/material inputs into specular/environment outputs; compare an independent reference if available. Record shader identity/hash, relevant constants and discriminating test before changing shader behavior. Current captures alone do not justify global lighting, bias or TAA changes. No game launch automatically.

## Changes, validation and handoff
No new implementation or validation run in this resumed task. Preserve B32D40B0 tested baseline; do not install AAAD staged predecessor. Check independent Claude shadow review status before overlapping code; use a separate review record if status unknown. New owner records session/scope before editing. Save every substantive experiment here and promote reusable findings.
`;
files['docs/project-memory/templates/task.md'] = `# <objective>

Status: planned
Owner: <agent/session; coordination scope>
Updated: <date/time and timezone>
Scope: <paths/subsystem; exclusions; worktree/branch>

## Objective and success condition
<observable completion criterion>

## Checkpoint and evidence
<module/build identity; revision/dirty state; code/artifact paths+hashes; linked findings>

## Experiments and decisions
<exact command/input/config; actual outcome; evidence; conclusion+scope; alternative eliminated; failed attempt and retry condition; unresolved hypotheses>

## Changes and validation
<files/patches; checks+results; what was not validated; installed vs staged>

## Next action and handoff
<one concrete first action; dependencies/blocker+restart criterion; running process ownership; risk/limits; follow-up objectives>
`;
files['docs/project-memory/templates/finding.md'] = `# <stable topic / ID>

Status: hypothesis
Updated: <date/time and timezone>

## Claim and scope
<specific observation or testable hypothesis; game/module/build; known limits>

## Identity and relationships
<symbol+aliases; address space and offsets; signature/bytes; callers/callees; layout/ownership; unknowns explicit>

## Evidence and reasoning summary
<reproducible command/input/config; file/function+revision/hash or run+artifact; actual outcome; decisive argument, not private scratch reasoning>

## Alternatives / failed experiments
<conditions, result, what is excluded and what is not; retry criterion>

## Open questions / discriminating test
<smallest next test; dependencies>

## Supersedes / superseded by
<links; keep earlier evidence and why interpretation changed>
`;
// Refuse to replace an already-installed system or existing repository instructions.
for (const rel of Object.keys(files)) if (fs.existsSync(path.join(repo, rel))) throw Error('Already exists: '+rel);
const legacy = fs.readFileSync(workspace+'/AGENTS.md');
fs.mkdirSync(mem+'/archive', {recursive:true});
fs.writeFileSync(mem+'/archive/2026-11-06-legacy-instructions.txt', legacy, {flag:'wx'});
for (const [rel, content] of Object.entries(files)) {
  const dest=path.join(repo,rel); fs.mkdirSync(path.dirname(dest),{recursive:true}); fs.writeFileSync(dest,content,{flag:'wx'});
}
const bridge = `# Arceus shared project memory\n\nBefore work, read ${repo}/AGENTS.md and follow its startup instructions. Canonical memory is ${mem}/START.md. This directory is one part of the same local project; maintain the canonical records, not a second handoff. Existing outputs are dated evidence, not current instructions.\n\nKeep all game data and captures local. Preserve accepted runtime and user-owned tests. Do not launch automatically, send gameplay input, kill/restart a game, or overwrite a running test. See canonical instructions and current state before edits.\n`;
// Compare before replacing the sole pre-existing instruction file.
if (!fs.readFileSync(workspace+'/AGENTS.md').equals(legacy)) throw Error('Workspace AGENTS changed concurrently; reconcile manually');
fs.writeFileSync(workspace+'/AGENTS.md', bridge);
for (const base of [workspace, runtime]) {
  if (base!==workspace) fs.writeFileSync(base+'/AGENTS.md', bridge,{flag:'wx'});
  fs.writeFileSync(base+'/CLAUDE.md', '# Shared project instructions\n\n@AGENTS.md\n',{flag:'wx'});
}
console.log(JSON.stringify({repo,memory:mem,legacySHA256:crypto.createHash('sha256').update(legacy).digest('hex'),filesWritten:Object.keys(files).length},null,2));
