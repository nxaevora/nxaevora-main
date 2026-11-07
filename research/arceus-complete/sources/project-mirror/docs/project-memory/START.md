# Project memory entry point

1. Read [STATE.md](STATE.md) (current objective, completed baseline, next action).
2. Read that task's record and linked findings only. Use [INDEX.md](INDEX.md) or targeted rg searches for a different objective. Never recursively read this directory or the legacy outputs.
3. Check git status/branch and relevant file hashes. Saved paths, ownership and runtime state can be stale. Preserve unrelated work. Continue the recorded next action when the user gives no newer objective.

[WORKFLOW.md](WORKFLOW.md) defines updates, evidence and handoffs. Read it when writing records or coordinating agents. [ENVIRONMENT.md](ENVIRONMENT.md) maps this machine and the source/mirror split. [Templates](templates/task.md) are for new records, not startup reading.

Default resume: open this repository or its existing research workspace and say "Continue the project." No manual handoff prompt or journal maintenance is expected. The assistant maintains these files. A chat opened elsewhere needs the project path; memory is not global. Unsaved discoveries cannot survive a crash/interruption.

Keep AGENTS + START + STATE under 1,500 words combined. Keep STATE under 500 words. Detailed evidence belongs in topic/task files and is read on demand. These are size targets, not exact token measurements.
