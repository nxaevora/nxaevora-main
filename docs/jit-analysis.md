# JIT miss analysis

Subject: Pokemon Legends Arceus, recompiled with ScarletNX `8efbf9c`. Base game and update 1.1.1
(01001F5010DFA000, update 01001F5010DFA800).

## Correction of the first version of this file

The first version said that the first run used the update program. That was wrong. The base program
(`01001F5010DFA000.exe`) runs the base game code, even when the update is checked in the `.ini`. The update has its
own program (`..._01001F5010DFA800v262144.exe`) and its own module layout.

The first version also disassembled the pcs of the base run against the update modules. Its statements about single
pcs were about the wrong code, so they are removed. The sorting of the pcs by module in run A is still correct.

## The two runs

| | Run A | Run B | Run C | Run D |
| --- | --- | --- | --- | --- |
| Program | base | update | update | update |
| Hints at the start | none | 19 pcs, 17 of them in the wrong layout | 19 valid pcs | the same 19 pcs |
| Length | 45 s, boot to the language screen | 83.8 s | 15.4 s | 256.6 s, to the cutscene at sea |
| Misses | 57 | 51 | 0 | 114 |
| JIT compiles | 20 functions, 107 instructions | 17 functions, 92 instructions | none | 36 functions, 735 instructions, 151 KiB |
| New pcs in the hints | 19 | 17 | 0 | 36, all in main |
| Crash | none | none | none | none |
| Frame rate | about 30 fps | about 30 fps | about 30 fps | 30 fps, steady |
| subsdk0 / subsdk1 / sdk base | 0xc312000 / 0xc9b9000 / 0xd382000 | 0xc357000 / 0xc9fe000 / 0xd3c7000 | the same as B | the same as B |

Runs C and D used the update program with the 19 valid pcs as hints. In run D the JIT compiled 36 pcs, and the
hints file got 36 new pcs. The 19 pcs from before were therefore not missed again (inferred from the counts).

The bases of sdk and the two subsdk modules are 0x45000 higher in the update. The size of main is also different.
A hints file is only valid for the program that wrote it.

## Hints and the layout

- Run A wrote 19 pcs in base layout to `01001F5010DFA000.hints`. They were copied to the hints file of the update
  program and the update was recompiled. Of these 19 pcs, only the 2 in rtld are valid for the update, because rtld
  has the same layout in both versions. The other 17 pcs were marks at wrong places in the update code.
- In run B, the JIT compiled 17 pcs, and none of them is in rtld. So the 2 rtld misses probably did not happen
  again. This is read from the counts and is not proven directly.
- The hints file of the update program is now cleaned to the 19 pcs that are valid in the update layout.

## Misses of run B (update layout)

All 17 pcs are in the update layout and match the extracted ExeFS in `game/work/Arceus`.

| pc | Module | Offset | What is there |
| --- | --- | --- | --- |
| 0xc691510 | subsdk0 | +0x37f510 | PLT stub (`adrp`, `ldr x17`, `br x17`) |
| 0xcfff7f0 | subsdk1 | +0x6467f0 | PLT stub |
| 0x8c1e72c | main | +0xc1972c | in function, after a `cbz` |
| 0x8cbcb50 | main | +0xcb7b50 | in function, before a call through a vtable |
| 0x95170e8 | main | +0x15120e8 | epilogue, 2 instructions after the return site of a `blr` |
| 0x95b7b3c | main | +0x15b2b3c | in function |
| 0xa771378 | main | +0x276c378 | in function, after a `bl` return site |
| 0xaa60908 | main | +0x2a5b908 | block `mov x8, #...; b`, repeated at +8 and +16 (looks like `switch` cases) |
| 0xaac54d0 | main | +0x2ac04d0 | shared epilogue `ldp x29, x30; ret` |
| 0xae40adc | main | +0x2e3badc | **jump-table target** (verified, see below) |
| 0xd53fad0 | sdk | +0x1bdad0 | in function |
| 0xd555718 | sdk | +0x1d3718 | inside a loop body |
| 0xd555740 | sdk | +0x1d3740 | inside the same loop body, 0x28 bytes later |
| 0xd7827e4 | sdk | +0x4007e4 | inside `nn::util::VFormatString` |
| 0xd822390 | sdk | +0x4a0390 | blocks `mov w8, #n; str; ret` (looks like `switch` cases) |
| 0xd8fc7d4 | sdk | +0x57a7d4 | inside `vfprintf` |
| 0xd92f310 | sdk | +0x5ad310 | inside a loop of `__nnmusl_init_dso` |

## Verified: 0xae40adc is a jump-table target

The code before it is `adr x10, 0xae40ad8`, `ldrb w11, [x9, x8]`, `add x10, x10, x11, lsl #2`, `br x10`. The table
is at 0xb9a6394 and holds the bytes 1, 0, 8, 12, 8, 8, 8, 13. Each byte is a distance in words from 0xae40ad8.
Entry 0 (byte 1) gives 0xae40adc. This pc is only reached through the table, and the recompiler does not read the
entries of such a table. The mechanism of the notes below is therefore confirmed for this pc.

## How a miss happens (from the ScarletNX source)

- Generated code finds an indirect target in `CPU.disp`, one slot for each 4-byte pc. An empty slot calls the
  runtime JIT (`rt_miss` in `rtapi.c`). The pc is also recorded as a hint for the next recompile.
- A slot is filled for each leader (`leaders()` in `codegen.c`): the first instruction of a function, the
  instruction after B, BR, RET, BRK or an undefined word, a target of B or of a conditional branch, the instruction
  after BL or BLR, and each hint.
- The exploration of a function (`explore()` in `codegen.c`) follows B, conditional branches and BL. It stops at BR.
  The recompiler detects the base of a jump table (an address taken, then a BR within 6 instructions) so that it
  does not treat the base as a function. It does not read the entries of the table.
- `scarletnx -r` reads `<id>/<name>.hints` by itself. The runtime names the file after the program that started.

## Findings

- One miss is verified as a jump-table target (0xae40adc).
- Four more pcs look like `switch` cases (0xaa60908, 0xd822390, and the pcs inside `VFormatString` and `vfprintf`).
  This is not verified.
- Two pcs are PLT stubs. The stubs of rtld had the same kind of miss in run A.
- The other pcs are inside functions, and the cause is not known.
- The earlier finding about the instruction after an exclusive store is withdrawn. It was seen in the wrong code.

## Run E: the jump-table targets as hints

The update was recompiled with 45734 hints: the 45728 targets of the jump-table scan and the 55 pcs of runs B to D
(6 of them are not targets of a table). The recompile took 666 s (about 3 times longer than before). The code of the
update grew from 559575040 to 561684480 bytes (0.4 %).

The user ran the same scene as in run D, the opening scene at sea.

| | Run D | Run E |
| --- | --- | --- |
| Hints at the start | 19 | 45734 |
| Length | 256.6 s | 252.2 s |
| Frames shown | 7425 | 7207 |
| Misses | 114 | 0 |
| JIT compiles | 36 functions | none |
| New pcs in the hints | 36 | 0 |
| Crash | none | none |
| Frame rate | 30 fps | 30 fps, steady |

The start was not slower with the large hints file: the game started after 733 ms of preparation. The scene is the
only one tested. Other scenes can still have misses.

## Jump-table scan (`tools/jump_table_scan.py`)

The script finds the `br` instructions that use a jump table and reads the table from the module image. Two patterns
are found: `adr` base + loaded entry (byte, half or word) shifted by 2, and a signed word table relative to its own
address. The table size comes from the `cmp` and `b.hi` or `b.hs` before the `br`. A table whose address is in a
register that was set long before is resolved by a search back for the `adrp`+`add`.

Result for the update layout (`game/work/Arceus`):

| Module | Tables | Targets |
| --- | --- | --- |
| main | 1837 | 66124 |
| subsdk0 | 333 | 8082 |
| subsdk1 | 1244 | 69711 |
| sdk | 610 | 28494 |
| different targets | | 45728 |

Comparison with the 55 pcs in the hints file after run D:

- The pcs that are PLT stubs (4) are not targets of a table, as expected.
- Of the 51 other pcs, 49 are targets of a table found by the scan. This includes 35 of the 36 new pcs of run D.
- The 2 pcs that the scan did not find are 0x8cbe780 (main) and 0xd92f310 (sdk).

So almost all of the misses are jump-table targets. This confirms the mechanism for the whole set of misses seen so far.
The scan is a heuristic. It can list a target that no run reaches. The extra leaders are not a known problem, but
their effect on the size and the speed of the code is not measured.

## Next steps

1. Done: the experiment of run E worked for the opening scene.
2. Let the user play other scenes (village, wild areas, battle, menus) and read the new misses. Check whether the
   scan explains them.
3. Find out about the 2 pcs that the scan did not find (0x8cbe780 and 0xd92f310) and about the PLT stubs.
4. Done: the scan is part of the recompile procedure. `--merge` adds the targets to the hints file and keeps the pcs
   that the runtime recorded (see the skill `nx-recompile`). A hints file can hold any pc, so a pc inside a function
   can be given to the recompiler as a leader this way.
