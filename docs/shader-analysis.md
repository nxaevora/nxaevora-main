# Shader analysis

Subject: the shader programs that ScarletNX does not translate. Data: the four logs of 2026-10-31 (14:19 to 14:31, update
program, 85 s to 143 s each). The logs are in `game/recompiled/01001F5010DFA000/logs/` and are not part of the
repository.

## Numbers

- The start of each run translates the programs that the game ships: 1379 programs (`the game's own shaders`).
- The old figure of "80 of 1787 failed" cannot be reproduced. The log prints one line for each archive file that holds a
  program, so a shared program is counted many times. The 103 archive lines of one run add up to 160 failures. Counted
  by hash, **11 programs fail**: 10 vertex programs and 1 fragment program.

## The 10 vertex programs (translated before the game starts)

| Count | Message | Hash |
| ----- | ------- | ---- |
| 6 | `a jump table in a constant buffer` | fe3faabb, 80033a4b, a0a97109, 9bac6c0b, fdac039a and one more |
| 4 | `unknown opcode` with the word `000003b800000380`, `000003f0000003b8` or `0000039800000360` | a3c1291e, adb94885, 45a7cf5b and one more |

- Mechanism of the first group (from `pipeline.c`, `sh_spirv`): a program that ships with the game is translated before
  any draw. If the translation reads a constant buffer, the result is dropped, because a draw checks such reads against
  its own buffers and the pre-pass has none. The message names a jump table, but the code only tests that a constant
  buffer was read. So these programs are not broken. They wait for their first draw.
- The second group: the words look like pairs of 32-bit offsets that differ by 0x20 and 0x38. That fits the entries of a
  jump table that the decoder read as instructions. This is a reading of the words and is not verified against the
  disassembly of the program.
- The archives that hold them are interface scenes: `bag_top`, `bag_top_over`, `box_top`, `box_search`, `dressup`,
  `blost` and `battle_seq_intro` (also in every language copy). In the four runs none of the 10 hashes comes up again
  after the start. The runs did not draw them, or the draw did not fail. No draw with them is reported.

## The fragment program with `RED` (fails at the first draw)

- Hash 3aa06ea216ba378c, stage 4, instruction word `ebf9000000270406`. It is reported at about 2.2 s in every run.
- The count of skipped draws grows by exactly 600 in each 20 s window (3600 at about 123 s, 4200 at about 143 s in
  three runs). That is 30 draws a second, one in each frame at 30 fps.
- Reading: one draw in each frame is not drawn. This matches the `RED` program, but the log does not say which draw is
  skipped, so the link is an inference. The user has seen no fault, so the draw may not be visible (for example a
  helper pass).
- `RED` is a reduction atomic. The translator handles some forms (`global_atomic_opcode`) and returns
  `0xffffffff` for the others. The form of this word is the missing one.

## What is open

1. Decode `ebf9000000270406`: find which `RED` form it is and what the program does with it. Then decide if the form can
   be added to the translator (`spirv_translate.c`, ScarletNX).
2. Find the draw that is skipped each frame, to see what it draws. A log of the draw state at the skip would show it.
3. Check the 4 programs with the offset words against the disassembly, to see if they are jump table data.
4. Run the scenes of the 10 interface programs (bag, box, clothes, battle introduction) and read the log for draws
   that fail at the first use.

Items 1 and 3 need only files. Item 2 needs a patch of the runtime log. Item 4 needs the user to run the game.
