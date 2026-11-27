## Problem
With the text tick on (`SNX_ARCEUS_TEXT_TICK`, on by default), a text's glyph step `main+2B315E8` runs only on the text's own tick, 30 times a second. That step also holds the text's press tests: `main+FC10E4` in the wait at the end of a text, and `main+FC1078` at a page break. A press is an edge that the pad keeps for one frame. Above 30 FPS, a press that falls on a frame that is not the text's tick is never seen. At 120 FPS that is three presses in four. The save's "You saved your progress!" box could stay open through several presses, which is what the user saw in play.

## What changes
- `SNX_ARCEUS_TEXT_TICK_END` is on by default with the text tick. `0` turns it off. `1` and `3` (the experiment's value) keep it on, and any other value logs one line and keeps it on.
- On a step the tick would skip, the game's step runs when the text's last real step answered other than 1. That covers every wait: 0 at the end, 2 at a page break, 4 once closed. A text being revealed answers 1 and is skipped as before, so the reveal keeps its speed.
- The auto-advance keeps its length. In the end wait of modes 2 and 3 (`+90`), the step counts the byte `+A3` toward `+A2` (`main+2B31D44`) and leaves when it passes. An extra step that would complete the count is skipped, and one that counted gets the byte back. The count stays on the tick.
- Every 5 s an `arceus-text-tick: end=3` line gives the steps run, the counts held and the steps kept back. With `SNX_ARCEUS_TEXT_PROBE=1`, one line marks a text entering an auto-advance wait and one its leaving, with the time between.
- With `0` the text tick is as on main today. The docs tables and the game record have their rows.

## Testing
Automated, in `engine/tests/arceus_clock.c`, built with clang `-std=gnu11 -Wall -Wextra -Werror` with ASan and UBSan at `-O0` and `-O2`. All checks pass.
- The key's values: unset, `0`, `1`, `3`, empty, `2` and `00`.
- A shown text at `0`: the box's answers over 8 frames at 120 are 01110111, the 0 and 1 of the stuck box.
- With the fix: an end wait, a page break and a closed text answer on all 8 frames, a revealing text only on its ticks, and a wait ended by a press goes back to the ticks.
- Answers are kept only from the reveal loop and for the reveal's own text.
- The hold: for modes 2 and 3, thresholds 0, 1, 2, 5, 37 and 255, and counts from 0 and past the threshold, the text leaves on the same frame and `+A3` is the same after every frame as with the tick alone. A press on any frame of a mode 0, 1 or 2 wait, and of a page break, is seen on that frame.
- 18 mutants of the change, 17 fail the test. The one that passes reads a kept answer with the key at `0`, where none is ever kept.

## VERIFY-MATRIX
Axes: frame rate and render scale. The fault grows with the rate: the step runs on one frame in two at 60, one in four at 120 and one in eight at 240.

Rule, written before the runs: with the fix, every save box that is offered a press closes at that first press. The tables count the presses offered to a waiting box, so a box that stays through four presses counts four. One miss in any cell holds this PR. The runs with the tick alone are reported, not judged. A run counts only with its native NVN line, its save commit line and its box lines. The three runs on this branch's own build are the exception: the branch has no save probe, so they have no commit or box lines, and their boxes are read from screenshots (below).

The runs used the fork's development line with this change (build 0779e6, commit 47f49156), on the same PC, from a fresh save with a scripted save of 81 s. "Fix" is `SNX_ARCEUS_TEXT_TICK_END=3`, and "tick alone" leaves it unset there.

| Scale | Target | Menu rate | Arm | Report | Presses offered to a waiting box | Missed |
| --- | --- | --- | --- | --- | --- | --- |
| 1.0 | 120 | 120.0 to 120.4 | fix | d83181 | 2 | 0 |
| 1.0 | 120 | | fix | a73dae | 2 | 0 |
| 1.0 | 120 | | tick alone | 958ccf | 4 | 4 |
| 1.0 | 60 | 60.0 to 60.2 | fix | ffea67 | 2 | 0 |
| 1.0 | 60 | | fix | 03c053 | 2 | 0 |
| 1.0 | 60 | | tick alone | eba9b3 | 3 | 2 |
| 2.0 | 60 | 60.0 to 60.2 | fix | 8618a1 | 2 | 0 |
| 2.0 | 60 | | fix | a6c782 | 2 | 0 |
| 2.0 | 60 | | tick alone | f87253 | 3 | 1 |
| 1.0 | unlocked | 240.0 to 240.8 | fix | fc1e19 | 2 | 0 |
| 1.0 | unlocked | | fix | f3d302 | 2 | 0 |
| 1.0 | unlocked | | tick alone | 6cf47e | 4 | 4 |
| 1.0 | game (30) | 30.0 | fix | 74f006 | 2 | 0 |
| 1.0 | game (30) | | tick alone | 350140 | 2 | 0 |

With the fix: 18 presses offered, 0 missed, at 30, 60, 120 and 240 and at both scales.

Earlier runs the same day with the same script at scale 2.0 and target 120, with the fix and this code:

| Run | Build | Report | Presses offered to a waiting box | Missed |
| --- | --- | --- | --- | --- |
| SWH-2-e3 | 0779e6 | 65f079 | 2 | 0 |
| SWH-3-e3 | 0779e6 | 974ec4 | 2 | 0 |
| RGL-sw | e27b4f | 8a366d | 2 | 0 |
| RGL-sw-k | e27b4f | 171d29 | 2 | 0 |

e27b4f is the development line with RT-GI merged in. Its text tick code is the same as 0779e6's. RGL-sw-k ran with RT-GI's keys on, so the trace was running during the save. Runs of an earlier form without the hold are not counted. With the tick alone: 0 of 2 lost at 30, 3 of 6 at 60, 4 of 4 at 1.0/120 and 4 of 4 at 240. At the game's own 30 nothing is skipped, so both arms close every box and the fix's step count is 0. That is the baseline the model predicts. The save box is not an auto-advance text, so no count was held in these runs.

The user's play on the same build, scale 2.0, target 120, with a real controller (report fa3c2f):
- Seven saves, seven boxes, all closed. The user's word: the save fix worked perfectly fine.
- A wild battle was fought. Its five messages that leave by themselves (mode 3, threshold 0) each left one tick after their wait began, 24 to 33 ms, where without the hold they would leave after one frame. 52 extra steps were kept back.

On this branch's own build (7c8964, commit 50505470), with the same save script at scale 2.0 and target 120. The commits after it change only the test's comments and this text. The branch has no save probe, so the boxes were read from screenshots every 100 presents, checked against a run of the development line whose probe gives the boxes (382adb, SWH-2-e3 above):

| Run | `SNX_ARCEUS_TEXT_TICK_END` | Start line | Report | Presses offered to a waiting box | Missed |
| --- | --- | --- | --- | --- | --- |
| SBP-def-a | unset | `end=3` | dfdc76 | 2 | 0 |
| SBP-def-b | unset | `end=3` | 6ee139 | 2 | 0 |
| SBP-off | `0` | `end=0` | 23a589 | 3 | 1 |

In both runs with the key unset, the key is in neither the job's environment nor its settings, so the `end=3` line comes from the default, and every box closed at its first press. With `0`, the start line says `end=0`, and the second box stays through the press at 67 s, as with the tick alone on main.

## Remaining
- A message with a count of 40 (the battle's longer self-leaving messages, about 1.3 s at 30) was not seen in play. The host test covers thresholds up to 255, and the hold works the same way at every threshold. No played run has shown one yet.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
