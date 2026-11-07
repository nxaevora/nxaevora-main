# Local environment and provenance

Recorded 2026-11-06; verify paths and contents before use. These are local pointers, not portable dependencies. For a new machine, update this map; keep canonical knowledge in this repository.

| Role | Location |
| --- | --- |
| Git checkout / canonical memory | C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main |
| Research workspace, scripts, reports | C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett |
| Actual runtime build source (non-Git) | C:/Users/brett/Documents/Codex/work/nvn-src |
| Repository source mirror | third_party/ScarletNX/ |
| Runtime build | C:/Users/brett/Documents/Codex/work/nvn-build |
| Installed game | C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest |
| Extracted game/update | C:/Users/brett/Documents/Codex/work/arceus-dump |
| Test runs | C:/Users/brett/Documents/Codex/work/arceus-runs |

Git setup observation: HEAD 59c7e1780e50c74a8f92a53dcd77d017fd00cbf6, branch experiment/arceus-clock-hooks, staged and unstaged modifications already present. Do not reset/rebase/clean them as setup. Existing docs/arceus-*.txt are retained. No build command was independently validated during memory installation; recover exact invocations from the relevant experiment's scripts/link-arguments.json and build notes rather than inventing one.

Arceus 1.1.1 main build prefix reported in early timing investigation: AEE8F150DDA1B5A8; cached main base 0x08005000. Re-check full module identity and bytes before patches. main+ offsets are not runtime virtual addresses. Captured GPU addresses are capture-specific and may be reused.

Private tested shadow candidate: research workspace work/native-shadow-candidate/ArceusNativeShadow-runtime.exe, SHA256 B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019. Do not infer that the shared source/mirror contains its private changes. Inspect candidate inputs and hashes. Launcher: outputs/Arceus-Shadow-Controller-Test.exe; read launch notes before any authorized test.

This checkout is the durable documentation home, but old evidence files/captures remain elsewhere on this machine. They are not backed up by committing the memory. Retain local artifacts and include memory with normal repository version control when committing authorized work. No game data is copied into Git by this system.
