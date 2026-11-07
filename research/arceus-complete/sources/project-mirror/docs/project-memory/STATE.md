# Current project state

Updated: 2026-11-06 (America/Chicago). Findings imported from reports; memory setup independently verified the installed runtime hash below and found no ArceusTest process. Re-check live state before changes.

## Renderer update, November 7

[World path tracing](tasks/2026-11-07-world-pt.md) records the verified native-scenery runtime, optional vendor routes, independent repository build and experimental history results.

## Current objective and next action
The [neutral rim candidate](tasks/2026-11-06-neutral-rim-candidate.md) passed visual inspection in both F3 views of the crafting box. Carry the isolated candidate forward; installed B32 is still the original shadow baseline. Intended original-game shading remains unverified. [Native TAA](tasks/2026-11-06-taa.md), [DLSS](tasks/2026-11-06-dlss.md) and [FSR 3.1.6](tasks/2026-11-06-fsr.md) now have verified test launchers with estimated opaque-object motion. Vulkan replays covered jitter, walking, camera movement, turns, occlusion and history resets. The next manual gameplay test can use F3 to inspect vectors and adjacent scene features.

## Completed and accepted
- Shadow test timing-20261106-181617-047 closed normally (exit 0). User reports original stone/bridge/wheel pattern resolved; captured finals support this for tested scenes. See [shadow findings](findings/shadows.md).
- Tested runtime SHA256: B32D40B03CEE8ADAF34DA9150A657E97F25F9950E4CA25FBAD7F641CF8CAF019. Preserve it. The separately staged AAAD4627 runtime predates this accepted result and must not replace it casually.
- Accepted Vulkan configuration: target 120 FPS, camera 2.2, timing/menu/blink/effects corrections and controller support. 2560x1440 depth-tested scene viewports were observed; sustained displayed 120 FPS was not measured.

## Ongoing and unresolved
- Pink appears in scene lighting, absent in base color; material/specular/environment interpretation is a hypothesis. See task for capture locations and evidence.
- Broad timing/physics ownership and cadence remain incompletely established; see [timing findings](findings/timing.md). Do not apply blanket shared-solver multipliers.
- Claude had an independent shadow review in progress in prior notes. Its completion/ownership is unverified. Check before overlapping it; do not assume the old review is still running.

## Future objectives
User's requested order: resolve/accept pink-box result, then TAA, then DLSS, then latest available FSR3 release. Verify SDK versions/hardware/input requirements when integration starts. Earlier exploratory ray/path tracing remains later. No wholesale rendering replacement or unscheduled game test.

## Resume checks
Read [environment](ENVIRONMENT.md) before building. Checkout was dirty on branch experiment/arceus-clock-hooks at base 59c7e1780e50c74a8f92a53dcd77d017fd00cbf6 during setup. Runtime build source is outside Git; repository is its manually updated mirror. Latest reported test is closed, but check live state before touching binaries. Existing research has been indexed, not exhaustively revalidated or migrated.


