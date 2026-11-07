# Neutral rim-light workaround candidate

Status: completed
Owner: Codex chat01a1b37f-e7b1-7480-a4b0-a86a8a2654b7; isolated work/rim-candidate
Updated: 2026-11-06 America/Chicago
Scope: user requested trying a fix, then TAA, DLSS and latest FSR3 in that order

## Rationale and limits
Builds on [reflection test](2026-11-06-reflection-capture-test.md): explicit purple category1 color is a strong lead, but intended intensity and exact cause remain unproven. Prepared opt-in cosmetic workaround, NOT an emulation correctness fix. User was told it can affect other category1 surfaces; it does not identify the crafting box as an object. Never describe it as a proven bug correction or globally remove color.

## Concrete candidate
C:/Users/brett/Documents/Codex/2026-11-06/fix/outputs/Arceus-Pink-Box-Candidate.exe
Supporting outputs/rim-runtime/ArceusTest.exe SHA25676ade4bf725c459c7b33fe3a6727c0250db38e23c40fd560f9b2ee26eda8ee90.
Launcher retains controller/native shadows/Vulkan/1440p/120target/camera2.2/timing baseline and F3 diagnostics. Isolated --game launch route, verified B32 installed and candidate hashes, existing mutex/process checks, manual indefinite close. Sets SNX_ARCEUS_NEUTRAL_RIM=1; writes outputs/current-rim-test.txt. No live launch; B32 installed hash reverified unchanged.

## Implementation
fix/work/rim_candidate.inc: exact shader hashes89619027ba43e5d3/e02c1a109a77fac4, fragment stage4 bank3, size>=0x160; byte-exact matching color vectors at0x150 and0xd0 from all3 captures. Copies CPU-valid constants into private vk_stage uniform storage, converts only0x150 RGB to equal channels at linear luminance. Alpha stays. No guest writes or source asset/shader/normal/probe changes. Pending GPU constants/allocation failure retain original binding. Colors use standard linear weights. This preserves input luminance, not necessarily final postprocessed brightness. Diagnostics label enabled transformed CPU copies; allocation-fallback logs must be checked before treating these as actual GPU-bound constants.

Build script fix/work/build_rim_candidate.py inserts helper/call into verified reflection render source and links isolated object before archive, retaining shadow objects. Shared source/mirror unchanged. package_rim_candidate.py uses tested reflection helper with candidate path/hash/env/label/status-file changes. Provenance and package JSON in work/rim-candidate.

## Validation and next action
test_rim_candidate.py compiles actual helper with mocked upload functions against3 actual CB snapshots: exactly12 bytes changed, all other bytes/source/alpha preserved, neutral luminance0.28463954; wrong signatures/shader/stage/bank/size rejected; pending/allocation failure fallback tested. Compilation/link and launcher --verify-only passed; existing scissor initializer warning only. Visual outcome is untested.
User runs prepared launcher, F3 box at two angles and inspects nearby wood/stone/characters, saves/closes. Check log arceus-neutral-rim applied/skipped and capture before judging result. Compare new images with190711 captures without claiming pixel identity. Reject or refine if unrelated surfaces worsen. No automatic launch or installation.

## Follow-on sequence
User explicitly requests TAA after this issue, then DLSS, then latest FSR3 release. Preserve this order; SDK/latest version and hardware capability must be verified at integration time. First temporal step is inventorying camera jitter, motion/depth inputs, history and camera-cut resets, and UI exclusion. No temporal implementation yet; avoid promising frame generation or particular SDK version before verification.

## Closed game test
Run timing-20261106-192021-268 exited 0. Both F3 final images show neutral box scratches. In the frontal lighting ROI the prior capture had 1740 purple samples; candidate had zero of 93500. Camera poses are similar, not identical. Carry the isolated candidate into TAA; do not overwrite B32.

