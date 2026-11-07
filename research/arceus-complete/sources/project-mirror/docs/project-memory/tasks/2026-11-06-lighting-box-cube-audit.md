# Pink box: recovered last-run context and cube input audit

Status: ongoing
Owner: Codex chat 01a1b37f-e7b1-7480-a4b0-a86a8a2654b7, independent read-only analysis
Updated: 2026-11-06 America/Chicago
Scope: last-run evidence recovery and missing lighting-input audit

## Evidence
Continues [lighting investigation](2026-11-06-lighting-box.md). Viewed screenshot_20261106_184316_buffers_1_final.png from the memory-setup workspace. Purple scratches and edges are visible. No matched reference establishes whether this is intended.

Compared shadow-off/materials.json lighting configurations for e02c1a109a77fac4 and 89619027ba43e5d3 with shadow-off/attachments.json by GPU address. None of the environment cube input addresses in these configurations appears in the 64-attachment capture. Reproducible extracted table: C:/Users/brett/Documents/Codex/2026-11-06/fix/work/cube-input-audit.json. This is first-occurrence configuration evidence, not proof of the exact shader/binding active at the screenshot pixel.

Config189 cube words34/36 use addresses57a8f0000/57a910000, both64x64x6 BC6H signed float (vkfmt144), with1/7 levels. Earlier default-probes.py analyzed uncompressed default half-float assets; it did not validate these cubes. The captured six-face texture53ac94000 is a different address/format and cannot substitute for them.

Guest e02 shader word229 samples cube word34, followed by diffuse-color multiplication and cb3 offsets960/964/968 scaling. Word232 samples cube word36 with explicit LOD and reflected direction; words267/268/275 multiply this sample by reflectance terms. These are relevant paths, not a proven source of purple. Current translator TEXS encoding13 uses Ra,Ra+1,Rb coordinates and Rb+1 LOD; no demonstrated register-selection defect found in this inspection.

## Limits and next action
Cannot reconstruct the lighting contribution from saved attachment data alone: exact per-draw constants and active cube mip contents are absent. Prepare bounded capture of those values on a user-requested test, or identify exact source probe assets and a matched reference. Do not change colors, shadow bias, BC6 format or shader logic speculatively. No runtime/source edits or launches performed. Accepted installed hash independently verified B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019; no ArceusTest process at inspection. Existing dirty Git state preserved.
