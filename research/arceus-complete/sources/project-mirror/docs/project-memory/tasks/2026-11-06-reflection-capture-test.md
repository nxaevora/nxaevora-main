# Pink-box reflection capture test

Status: ongoing
Owner: Codex chat 01a1b37f-e7b1-7480-a4b0-a86a8a2654b7; private diagnostic sources
Updated: 2026-11-06 America/Chicago
Scope: prepare user-requested capture test, no automatic game launch

## Objective and evidence
Continues [cube audit](2026-11-06-lighting-box-cube-audit.md). Existing captures excluded ordinary textures and compressed formats and only saved mip0. Prepared separate cube readback for BC6H signed/unsigned and supported uncompressed formats, all faces/layers and all mips up to16. Bounds256 mip entries/64MiB; within2 presents. Same existing Vulkan finish/readback/barrier/restore path; ordinary attachment capture remains intact. No color/rendering correction proposed.

## Prepared artifact
C:/Users/brett/Documents/Codex/2026-11-06/fix/outputs/Arceus-Reflection-Test.exe
Supporting outputs/reflection-runtime/ArceusTest.exe SHA256 f2c79bf748754d22cb2c886665b739134934389c00bd52f519d8c4a21d6ce351.
Launcher verifies installed B32 and diagnostic hashes, uses isolated diagnostic executable with --game pointing at installed game folder (name_game/game_option inspected), same GAME cwd and controller environment. No installed executable replacement, no kill/timeout/automatic test. Existing mutex/process guards preserved, diagnostic process remains named ArceusTest.exe. Native shadows explicitly ON.

## Implementation and validation
Private workspace C:/Users/brett/Documents/Codex/2026-11-06/fix/work/:
prepare_reflection.py generates/compiles isolated surface.c/render.c from verified native-shadow candidate source hashes, links before existing archive and native-shadow texture object. reflection_bindings.inc retains per-draw descriptors and CPU constant snapshots for e02c1a109a77fac4/89619027ba43e5d3 (eight draws per latest frame). GPU-pending/unmapped constants explicitly omitted; these are not GPU uniform readbacks. Metadata includes recorded and capture presents, skipped draws, write failures. Potential exact-pixel causality remains unproven.

verify_reflection.py rebuilds baseline using identical basename: .text/.rdata/.data hashes match accepted executable. Different basename initially moved linker export/import data and produced false mismatch; no evidence of changed shared archive. Actual C size-helper fixtures pass BC6 signed/unsigned six-face chains including tiny mips, half float, stale/noncube/undefined/oversize checks. Compile/link passed with one existing scissor initializer warning. package_reflection.py builds native helper and --verify-only exits0 without game. Proofs in work/reflection/{provenance,verification,package}.json. Shared source/mirror/installed binary untouched.

## Next action and success criteria
User opens prepared launcher, returns to pink box, F3 at two nearby angles, saves/closes manually. Full instructions in outputs/Reflection-Test-Instructions.txt. No live validation yet. Check outputs/current-controller-test.txt, log for controller and capture failures, then run work/check_reflection_capture.py against each *_reflections.tsv. Verify bound word34/36 addresses have complete faces/mips and constant coverage; inspect cap/unsupported/pending flags before replay. Decode actual captured probes and compare lighting contributions; do not infer source asset identity from defaults. Preserve original raw captures. If capture lacks a shader variant or GPU constants, extend only that missing coverage.

## Completed live test and decoded evidence
The preceding next action is completed. Run timing-20261106-190606-402 closed exit0; DualSense Edge and both sensors detected; no process remained at inspection. User reports still pink. Captures190711/190722/190723 each have15 cube mip entries/3addresses,64 attachments,one lighting draw89619027ba43e5d3,zero dropped/failures/bounded-out. Coverage checker passes all three. Constants1/3/4/16 valid CPU snapshots; draw present4164/4801/4856 vs capture4165/4802/4857. Installed B32 hash still unchanged.

Local CPU BC6H decoder tests/support/bcn.c compiled by fix/work/decode_cube.c; analyze_reflections.py decoded all45 mip payloads. Actual bound diffuse57a8f0000 has0 classified-purple texels; specular57a9a0000 has1 at mip0 and0 at remaining mips, with ordinary outdoor scenery. Definition R/G>1.5,B/G>1.5,G>.001. These ranges do not prove Vulkan sampling/translation correct.

New static-dataflow lead: basecolor alpha70 -> guest word328 BFE(offset2,width3) -> category1; word624 shifts category by4; words659/660/661/663 read cbuf3 color rows0x150/0xd0. Captured0x150 linear RGBA=(.45641106,.18782076,.73791051,.47058824), sRGB RGB180,120,223, identical in all3 captures. All1740 classified-purple pixels in ROI x700..1799,y735..819 select category1. Guest lighting shader subsequently blends these color rows into output. Thus game-supplied purple rim/highlight color is a strong explanation, NOT yet proof of proper strength/normal response or exact per-pixel causality.

Evidence: fix/work/rim-evidence.json, decoded-reflections/summary.json, lighting-pixels.json; outputs/Reflection-Test-Results.txt. probe_lighting.py ISCADD index was corrected during analysis to (materialBlue<<8)+materialAlpha=156, not624; final script uses correct instruction fields. Exploratory read_lighting_assets.py identifies Rim records in7 loose .trlgt assets but does not find the captured color there; do not claim the exact source asset was identified.

Current next action: isolate/replay rim contribution from saved data or matched original-game reference. No repeated capture needed yet. Any neutralized rim diagnostic would demonstrate contribution but would not by itself be a correctness fix. No rendering source/binary changes or launches during analysis. Pink root cause remains unresolved; do not globally remove purple or label intentional lighting proven.
