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
- The second group is jump table data, read as code (checked, see the next section).
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

## Patch 0002: `RED.E.ADD.64` added (built and run once)

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
- Run result (user, 2026-10-31, update program, two runs of 43 s and 157 s, from the first start into the game): the line
  `shader 3aa06ea216ba378c ... not translated` is gone, and the count of skipped draws is **0** in every window of
  20 s (before: 600 in each). There is no crash, no failed pipeline and no Vulkan error in the log. So the driver
  (NVIDIA, RTX 2060 SUPER) accepts the aliased 64-bit view. The frame rate is 30.1 in the steady windows (31 windows of
  5 s: average 29.6, minimum 22.4 at the start). 0 JIT misses.
- Not known: what the screen showed. The user must say if a picture changed, because the draw that was skipped before
  is now drawn.

## The 4 programs with offset words (checked against the files)

Method: the extracted RomFS (`game/work/Arceus/romfs`) holds the archives. The Yaz0 layer and the SARC layer were read
with a script in the scratchpad (nothing of the game was written to disk). The program of `bag_top_00.arc`
(`bgsh/__ArchiveShader.bnsh`, vertex program, 395 words with its SPH) holds the word `000003b800000380`. The opcode
names come from `maxwell_opcodes.def`.

- The word is not an instruction. The program ends with `EXIT`, `BRA` (a loop to itself) and `NOP`s. Directly after
  them are 13 32-bit entries (0x330, 0x358, 0x380, 0x3b8, 0x3f0, 0x430, 0x460, 0x4a0, 0x4d0, 0x500, 0x530, 0x560, 0x5a0)
  and then one word 0x40000000 and zeros.
- The program has one `BRX` with the usual switch form: `IMNMX` (the clamp, 12, so 13 entries), `SHL` by 2, `LDC` (the
  load of the entry), `BRX R13, -816`. The offset -816 is 0x330, so the entry 0 is the instruction after the `BRX`.
  Each of the 13 entries gives a target that is a real instruction (`FADD`, and `MOV` for the last one). So the 13
  entries are the jump table, and the table is stored in the program's own data after the code.
- Why the pass before the start fails: it has no constant buffer to read (`ship_no_read`), so the table is unknown and
  the length scan (`shader_hash`) has no end for the program. It stops only at the first all-zero word, which is
  after the table. So the program length includes the table, and the translation meets the table's words:
  `unknown opcode`. The first group of 6 is the same kind of program. The table in them is not next to the code, so
  the length is right, but their translation read a constant buffer, and the pass drops such a result (message `a jump
  table in a constant buffer`).
- All the hits with `000003b800000380` (the programs in `bag_top`, `bag_top_over`, `blost`, `box_search`, `box_top`,
  `dressup`) have the same form: the table directly after the padding of the program, a run of 6 non-zero words, then
  zeros.
- The other two words are the same form. `000003f0000003b8` is in `main.data` (the module `main`, guest 0xc049f08, the
  table directly after `EXIT` and `BRA`), and `0000039800000360` is in `fade_common_00.arc` (`EXIT`, `BRA`, `NOP`s,
  then the table). `main.data` is the data segment of the `main` module, read with
  `scarletnx --dump-modules <ExeFS folder> <folder>`. The word `000003b800000380` is also in `main.data` (guest
  0xc047808). So the 4 programs that fail with an unknown opcode are: `a3c1291e` and `adb94885` (`000003b800000380`),
  `45a7cf5b` (`000003f0000003b8`, in `main.data`) and one of `fade_common_00` (`0000039800000360`). All 4 have the table
  right after the code.
- The 6 programs of the first group have the table apart from the code. Checked on one: in `bag_top_over_00.arc` the
  table at 0x17400 comes after `EXIT`, `BRA`, `NOP`s and 24 zero words. The length scan stops at the first zero word,
  so the table is not part of the program. The other 5 were not opened.
- So the two groups have one cause. The pass before the start has no constant buffer, so a `BRX` table is unknown. Where
  the table is right after the code, the scan runs into it (4 programs, unknown opcode). Where zeros come between, the
  scan stops in time, the translation succeeds, but it read a constant buffer and the pass drops the result (6
  programs).
- Reading: this is not a missing opcode. A draw reads the table from its bound constant buffer, the targets are known,
  and the scan ends at the `EXIT`. The 10 programs wait for their first draw. This is not proven for the draw: none
  of the 10 was drawn in the runs. The scenes that draw them (bag, box, clothes, battle introduction) are not tested.

## Run of 681 s (user, 2026-10-31, bag, box, clothes and battle introduction scenes asked for)

- No `not translated` line after the start, and none of the 8 hashes that the earlier logs name comes up after the
  start. 0 skipped draws in every window, no crash, no Vulkan error, 0 JIT misses, 30 fps (135 windows of 5 s: average
  29.9, minimum 23). The slowest frame is the stop at the start (1248 ms at 14.8 s).
- The programs found by the draws grew from 142 to 191 of the 1380 shipped ones. 91 programs were translated at draw
  time (26 ms in all), and each one succeeded. A program that failed before the start and is drawn is one of them,
  but the log has no hash for a success, so the run does not show which scenes drew the 10 programs.
- The user opened the bag, the box, the clothes and the battle introduction, and explored a large part of the open
  areas. So the 10 programs translated at their first draw, and the check is **closed**: the failure before the start
  is the missing constant buffer of that pass, and it costs nothing in play.

## The texture of format 0

The line `unsupported texture format 0, header word 0 00000000 (not drawn)` comes once in each run (about 6.5 s to 10.5 s
after the start, in the first frames after the loading screen). Checked in the five logs of 2026-10-31 and in the
source (ScarletNX `render.c`, `texture.c`, `format.c`).

- The texture is not a texture of an unknown format. The header is empty: word 0 is 0, so the format is 0, and the
  address is 0. `tex_from_tic` returns no texture for a header without an address (`if (!gva) return NULL`), so the slot
  has no surface.
- The message comes from `vk_tex_num` (`render.c:828`): with no surface it asks the raw header's format, to tell if the
  sampler reads floats or integers. The format 0 is not in the table of `tic_format`, so it warns, once, and says
  float. The words "not drawn" in the message are misleading. The draw is not dropped.
- What the draw gets: a null view (`vk_null_view`, `render.c:1744`), which reads zeros, "as a missing texture does".
  The count of skipped draws is 0 in all the windows of all the runs, so no draw is dropped for this.
- In all five runs the line follows the same compute program at once: `vulkan: compute fa569ec6aa40d973 ready: block
  32x1x1, 10 global buffers, 1 textures`, then the line, then the first indexed draw (`mme: macro 30`). So the empty
  header is most probably the one texture of that compute program at its first dispatch. This is an inference from
  the order of the log lines. The log does not name the slot.
- Reading: the game binds an empty header to a slot that it does not use yet (a texture that the game writes later, for
  example the result of an earlier frame). The null view reads zeros, as a header with no address does on the console
  (this is not verified on the console, only from how the other emulators and this runtime treat it). The user sees no
  fault in any scene (the runs of 157 s and 681 s, the bag, the box, the clothes, the battle introduction and the open
  areas).
- Not known: if the empty header comes back in later frames. The line is printed once for each format, so the log does
  not count it. If a later change shows a wrong picture that this compute program feeds, add a counter at
  `vk_tex_num` that logs the handle, the stage and the draw's shader hash.

Decision: no change in the runtime. The wording of the message could be better, but a change to the submodule for a
log line has no gain.

## What is open

1. Find the draw that was skipped each frame, to see what it draws (the skips are 0 now, so this has little value).
2. Done: the scenes of the 10 interface programs were run (see the run of 681 s).
3. Done: the form of the programs of `main.data` and of the two other words (see above).
