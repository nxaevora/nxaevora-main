# In-game settings UI with Dear ImGui: plan

Status: plan only. No code is written. The facts about ScarletNX come from reading its source (`src/nx/runtime/platform/`,
`src/nx/gpu/vk/ui.c`).

## Goal

An in-game window for the settings, and for the text input that the game asks for (the player name), drawn with Dear
ImGui over the game. It is the base for the Tauri launcher: the launcher starts the game and edits the same `.ini`.

## Decisions

| Item | Decision |
|---|---|
| Latin font | Roboto (Apache 2.0) |
| Japanese font | Noto Sans JP (SIL OFL 1.1), merged into the same font atlas |
| Theme | ImGui dark theme, every blue color replaced with `#46478B` (hover and active states are lighter and darker steps of it) |
| Windows | Rounded corners |
| Alpha | 0.94 on the window background (`WindowBg`). Text and widgets stay opaque. `Style.Alpha` is not used, because it also fades the text |
| Text input | The game's software keyboard (the player name) is an ImGui window. It replaces the box of `overlay.c` |
| Entry point | A menu bar with About, Settings, Mods, Cheats and Debug. It shows when the mouse cursor is at the top of the window. No key opens it |
| Gamepad | ImGui does not use the gamepad. Mouse and keyboard only. The game keeps the gamepad |
| Launcher theme | The Tauri launcher does not share the theme values |
| First deliverable | The Debug window (see below). The other menus come after it |

## First deliverable: the Debug window

A window that the Debug menu opens. The concept has these parts, from the top:

1. Hardware: GPU load, temperature and power. VRAM used, total and clock. CPU load, temperature and power. RAM used,
   total and the game's own use.
2. Frame: FPS, frame time and the maximum frame time, and a graph of the last frames.
3. Timings: CPU frame preparation, CPU busy time per frame, pipeline waits (ms) and texture uploads per frame.
4. Display settings: window size, game resolution scale, scaling, anti-aliasing, frame limit and V-sync.

Sources to find in step 1 (a value that no source gives shows as `?`, as the stats box of ScarletNX does):

- FPS, frame time, CPU load and process memory: `stats.c` already measures them.
- GPU load, temperature, power, VRAM and clocks: not in ScarletNX. Needs a vendor library (NVML, ADL) or a Windows
  API (DXGI for VRAM, PDH counters for load). Temperature and power may not exist on every machine.
- CPU temperature and power: no standard Windows API. Show `?` when there is none. Do not add a kernel driver.
- The timings of the renderer: the Vulkan side must count them (`vk_submit`, pipeline creation waits, texture uploads).
- The display settings: the keys in `settings.txt`. Check which ones exist and which are new.

## What stays in ScarletNX

- `fatal_screen.c` (crash panel), `prepare_screen.c` (shader screen before the game) and `hud.c` (Debug warning
  line) stay. They must work when ImGui is not running.
- The submodule is not edited unless a change is necessary (project rule). The preferred way is a patch in
  `recomp/patches/` with a short README entry, as for the patches `0001` to `0012`.

## Facts that shape the design

- The UI is a list of quads (`SnxUiList`) that `vk_ui_draw` (`gpu/vk/ui.c`, called at `gpu/vk/renderer.c:446`) draws
  over the swapchain image. The SDL backend has its own path (`ui_draw_sdl`, `backend/sdl3.c`).
- The menu changes on the main thread and the renderer draws on its own thread. Both use `ui_lock`.
- The keyboard hook is `overlay_keyboard_show`, `overlay_keyboard_hide`, `overlay_keyboard_type`,
  `overlay_keyboard_key` and `overlay_keyboard_take`. The game side calls `backend_keyboard_show/hide`
  (`backend/sdl3.c`). The answer is taken once with `overlay_keyboard_take`, which returns text, cancel, and the ticket.
- Input goes through `overlay_event` (`backend/sdl3.c`) before the game sees it. F2, Back and Home open the menu.
- The settings come from one table, `settings.txt`, which generates `g_settings` and the `.ini` tables (`ini.h`).

## Steps

1. **Investigation** (agent `game explorer`, read-only). Answer these from the source:
   - Where to record ImGui draw data in the Vulkan frame (after `vk_ui_draw`, same render pass or its own), and what
     the renderer thread may touch.
   - How `ui_lock` and the frame loop interact with a second UI that draws from the renderer thread.
   - Which events `overlay_event` takes first, and the exact way to hand them to ImGui and back.
   - How IME input reaches the game now (`overlay_keyboard_typing` gives the line rectangle for the IME window).
   - Whether the SDL fallback backend needs ImGui (headless and SDL renderer paths), or only Vulkan.
2. **Dependency.** Add Dear ImGui (MIT) with the SDL3 and Vulkan backends. Pin a release and its SHA-256, as the
   other libraries of ScarletNX are pinned (`cmake/dependencies.cmake` style). Add it to the credits with Roboto and
   Noto Sans JP.
3. **Fonts.** Put the font files in the repository (they are not game data). Load Roboto at the base size and merge
   Noto Sans JP with the Japanese glyph ranges. Check the size of the atlas and the load time. If the atlas is too
   large, use a glyph subset that the settings text and the name input need.
4. **Backend.** Initialize ImGui on the renderer side, record its draw data after the game and after the existing
   quads, and shut it down with the swapchain. Scale with the display scale of the window.
5. **Style.** One function sets the theme: dark base, `#46478B` in place of each blue, rounding of the windows (and
   of frames, popups and scrollbars), `WindowBg` alpha 0.94. Keep it in one file so the launcher can reuse the
   values.
6. **Input.** The menu bar shows when the cursor is at the top of the window. While a menu or a window has the
   focus, ImGui gets mouse, keyboard and text events first, and the game does not. ImGui reads no gamepad events.
   Close the old menu keys so that the two do not both open. The native mouse look must stop while the cursor is
   over an ImGui window or the menu bar.
7. **Debug window first.** Build the menu bar and the Debug window of the concept, then the Settings window.
   **Settings window.** Build the pages from the `settings.txt` table, so the `.ini` and the window cannot differ.
   Start with the keyboard map, the mouse look values (`mouse_native_*`, `mouse_sensitivity`) and the stats corner.
   Write changes to the `.ini` through the existing `ini.c` functions.
8. **Name input.** Replace the box of `overlay.c` with an ImGui window that is driven by `overlay_keyboard_show`
   and `overlay_keyboard_hide`. It returns text through `overlay_keyboard_take` with the same ticket. Keep the limits
   that the game sends (maximum length, number-only kind). Enter answers, Escape cancels. Give the IME window the
   position of the text field.
9. **Disable the old menu.** Turn off the open keys of `overlay.c` in the patch. Keep its code, so
   `fatal_screen.c`, `hud.c` and `prepare_screen.c` and the other users of `ui.c` still build.

## Verification

- A build is not a test. The user runs these scenes (small tests, one change each):
  1. Open and close the window in the game, change a value, see that the `.ini` holds it after the exit.
  2. Start a new game and enter the player name with the keyboard, then with the IME in Japanese. Cancel once.
  3. Open the window during a cutscene, a battle and a menu of the game, and close it. The game must keep its state.
- After each run, read the log and report (skill `nx-log-read`).
- Check that the crash panel and the Debug line still show (a forced fatal error is not needed: read the code path).

## Risks

- A second UI on the renderer thread can break the lock order. Step 1 must show the order before any code.
- Noto Sans JP is large. A full atlas costs memory and start time.
- The Japanese IME needs the text input rectangle. If the position is wrong, the candidate window hides the field.
- The patch grows the list of changes to the submodule. Each new patch needs the rebuild steps in `WORK_PLAN.md`.

## Open questions

- Which keys of the game are free for the keyboard while a window has the focus (the mouse look and the keyboard map
  use the keyboard and the mouse).
- Whether the hardware readings (GPU and CPU temperature, power) are worth a vendor library in the first version.

## Findings of step 1 (source reading, no run)

- **Draw point.** `show()` in `gpu/vk/renderer.c` clears the swapchain image, blits the game, calls `vk_ui_draw` (line
  446), then moves the image to present. The old UI uses dynamic rendering with LOAD and STORE. ImGui can use its own
  dynamic rendering scope between line 446 and line 448, then invalidate the cached render state (as `ui.c:402` does).
- **Threads.** `show()` runs on the GPU thread and on the main thread (refresh), under `vk_lock`. With `vk_record` on,
  commands go to a recorder thread (`rec_Cmd*`). ImGui commands must go through the recorder or after a
  `vk_rec_sync()` boundary.
- **Locks.** Observed orders: `vk_lock` then `ui_lock`, HID `input_lock` then `ui_lock`, AM lock then `ui_lock`.
  Never hold `ui_lock` while asking for Vulkan, HID or AM work. ImGui gets one context owner and publishes immutable
  draw data.
- **Events.** The order is `press_event`, `overlay_event`, then the game switch (`backend/sdl3.c:1488-1491`). The ImGui
  gateway goes first. Consumption comes from the ImGui capture flags, not from the backend's return value.
- **Mouse look.** `mouse_look_event` adds `xrel/yrel` to the guest float pair (`sdl3.c:1195-1234`). It must be skipped
  while ImGui has the mouse.
- **Keyboard and IME.** Only committed text is handled (no preedit). `keyboard_sync` owns `SDL_SetTextInputArea`.
  One owner must arbitrate it between ImGui and the old keyboard. The ticket protocol stays.
- **Scope.** ImGui is needed for windowed Vulkan only. The SDL renderer path is the fatal screen. Headless skips ImGui.
- **Counters.** FPS, frame time and process CPU and memory exist in `stats.c` (the CPU value is the process share of the
  machine). Pipeline waits (`pipeline.c`) and texture upload counts (`texture.c`) exist as cumulative counters. CPU
  frame preparation and CPU busy time per frame do not exist and need new timers.
- **Settings.** `resolution`, `fps_limit` and `vsync` exist. Window size, scaling filter and anti-aliasing do not.
- **Defect found.** `renderer.c:463` and `:491`: the Tracy plot "texture uploads" uses the byte counter, not the count.

## Decisions after step 1

- **Reaching the menu bar.** The mouse is released while a chosen key is held, in addition to the top-edge bar.
  The key is chosen in step 6.
- **Display settings.** Window size shows the current window size and is read-only. Scaling and anti-aliasing show as
  items only (disabled, no setting behind them yet).
- **Hardware values.** Vendor libraries are used (NVML for NVIDIA, ADL or ADLX for AMD, and the best option for Intel
  and for the CPU). A value that a machine does not give shows as `?`. Step 1 of the Debug window work must check the
  license and the redistribution terms of each library before it is added.
- **Working style.** Each step ends with a report to the user. The next step starts after the user says so.

## Progress

- Step 1 (investigation): done.
- Step 2 (dependency): done. Patch `0013-imgui-dependency.patch` pins Dear ImGui 1.93.0 (MIT). The configure of
  ScarletNX downloaded it and checked the SHA-256. Nothing is compiled yet. The credits entry for ImGui, Roboto and
  Noto Sans JP comes with step 3, when the font files are added.
