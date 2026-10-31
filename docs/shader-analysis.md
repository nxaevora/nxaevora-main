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

## Decode of the `RED` word

Fields of `ebf9000000270406`, read with the bit positions of `global_atomic_opcode` (`spirv_translate.c:320`):

| Field | Bits | Value | Meaning |
| ----- | ---- | ----- | ------- |
| value register | 0 to 7 | 0x06 | R6 (a 64-bit value uses R6 and R7) |
| address register | 8 to 15 | 0x04 | R4 |
| size | 20 to 22 | 2 | 64-bit unsigned integer |
| operation | 23 to 25 | 0 | add |

So the program does `RED.E.ADD.64`, a 64-bit add into global memory. The function returns 0 for each size other than
0, 1 and 3 (the 32-bit forms and the float add), so the translator rejects the program. The size 2 is the missing form.
The program is a fragment program, and the draw is skipped in each frame because of it.

What the translator would need (not started):

- The module must declare the capabilities `Int64` and `Int64Atomics`, and the device must have the feature
  `shaderBufferInt64Atomics`. The runtime asks for `shaderInt64` now (`device.c:253`) and does not ask for the second.
  Whether the RTX 2060 SUPER has it is not checked here.
- The pointer code (`global_atomic_pointer`) gives a pointer to one 32-bit word of the buffer. A 64-bit access needs a
  64-bit view of the same buffer.
- An emulation with two 32-bit atomics is not exact. The pair is not one atomic step, and the carry between the two
  halves can be lost when two invocations add at once.

What the program does with the 64-bit counter is not known. A 64-bit add in a fragment program is usually a counter or
a sum over the pixels (for example statistics or exposure).

## Patch 0002: `RED.E.ADD.64` added (built, not yet run)

The user decided to add the form. The patch is `recomp/patches/0002-global-red-add-u64.patch` and it changes the
working tree of the submodule (not committed, like patch 0001).

- The module declares `Int64` and `Int64Atomics`. Each global buffer is also declared as an array of 64-bit words on
  the same binding, and both variables carry the decoration `Aliased`. The atomic adds through the 64-bit view
  (`OpAtomicIAdd` on a `ulong` pointer). The pair of registers R(d) and R(d+1) is the value.
- The device asks for `shaderBufferInt64Atomics` (Vulkan 1.2 feature). A device without it makes `vk_spirv_unavailable`
  refuse the program, which is the old behavior.
- Only the add is added. The other 64-bit forms of `RED` and of `ATOM` stay refused.
- The word index goes through the page map as before and is divided by 2. This holds when the address is a multiple
  of 8, which the instruction needs. An address that is not aligned is not checked.
- Checks done: the build, the ten shader tests of `ctest`, and a new regression test. Its output passes `spirv-val`
  with the target `vulkan1.3`. The disassembly shows the two capabilities, the two `Aliased` variables and one
  `OpAtomicIAdd %ulong`.
- Not done: a run of the game. A build is not a test. Whether the driver accepts the module and whether the skipped
  draws go to 0 needs the user's run (see below).

How to read the run: the log must not have the line `shader 3aa06ea216ba378c (stage 4) not translated`, and the count
of skipped draws must stop growing by 600 in each 20 s window. If the log shows a failed pipeline for this program,
the driver did not accept the aliased 64-bit view.

## What is open

1. Find the draw that is skipped each frame, to see what it draws. A log of the draw state at the skip would show it.
2. Check the 4 programs with the offset words against the disassembly, to see if they are jump table data.
3. Run the scenes of the 10 interface programs (bag, box, clothes, battle introduction) and read the log for draws
   that fail at the first use.

Item 2 needs only files. Item 1 needs a patch of the runtime log (it may not be needed if the run of patch 0002 shows
that the skipped draws are gone). Item 3 needs the user to run the game.
