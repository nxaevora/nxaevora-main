# Timing and engine evidence map

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
