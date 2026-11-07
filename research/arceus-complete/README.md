# Arceus development archive

This archive preserves the earlier timing, camera, menu, controller, shadow, shader translation, memory writeback, motion estimation, upscaling and world path tracing work alongside the maintained renderer in [runtime/gfx/world-pt](../../runtime/gfx/world-pt/README.md).

The inventory maps each audited source file to its published copy and records its SHA-256 after CRLF normalization. Identical source files share a published copy. Large modified runtime translation units are stored as individual unified patches against the recorded ScarletNX base file named by the inventory's `base` field. Each patch represents one independent snapshot. Its original source hash is recorded for reconstruction.

The source roots cover the November 6 project research workspaces, November 7 renderer workspace, offline runtime benchmark scripts, modified native runtime and project mirror documentation and tests. SDKs, compiler installations, upstream repository checkouts and package caches are outside the authored-source inventory. Entries marked `local_only` identify generated shader arrays or guest instruction fixtures.

The original research directory structure is retained beneath `sources`. Scripts can depend on the local directory layout, private game inputs, locally installed tools and earlier intermediate results. Read each script before running it and configure its input paths. Snapshot source files document experimental and superseded behavior. Use the maintained renderer's preparer and build instructions for the validated integration.

The inventory includes the earlier project memory and task records with their original evidence scope. Some records describe earlier milestones. The November 7 renderer handoff and maintained renderer README record the latest validated release.

Game assets, saves, compiled executables, vendor DLLs, raw captures and logs remain in the local research workspaces under the project's persistent sharing instructions.

Run `python research/arceus-complete/verify_inventory.py` from the repository root to check all published source mappings and reconstruct the archived patches in memory.
