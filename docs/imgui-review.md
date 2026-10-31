<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# ImGui review

Reviewed patches 0013-0019 against the applied files. Locations below are relative to third_party/ScarletNX unless stated otherwise. This is a source review. No source was changed, no build or test was run, and the game was not started.

Checks without a separate finding: vk_ui_draw leaves TRANSFER_DST_OPTIMAL. vk_rec_sync drains recording before the direct ImGui commands, and ImGui restores the layout expected by show(). Swapchain retirement drains recording and waits for the device before shutdown. Reinitialization takes the new format and at least the frames-in-flight buffer count. The bundled backend copies the attachment-format array, so its initialization pointer is not dangling. The statistics ring, hardware snapshot, and event queue have mutex protection. The hardware thread copies the GPU name under its mutex. Its process lifetime alone does not establish a dangling NVML handle. All 15 new source/header files have the required SPDX and copyright lines. Patch paths contain no game files or binary sections. Public C functions have declarations, and recorded C compile commands retain -Werror=missing-prototypes. These checks do not replace runtime validation.

## 1. [P1] Vulkan errors are ignored, so backend failure cannot trigger the fallback

**File and line:** src/nx/gpu/vk/imgui_glue.cpp:48-68, especially 63; src/nx/gpu/vk/imgui.c:151-153.

**Fault:** No CheckVkResultFn is installed. In the bundled backend, check_vk_result then does nothing. ImGui_ImplVulkan_Init returns true even when device-object creation encounters Vulkan errors. Thus g_failed does not catch these errors. Later buffer mapping/allocation failures can continue with invalid handles or null mapped pointers. The alive stamp can also hide the legacy keyboard despite failed rendering. This violates the contextual-error and explicit-fallback rules.

**Concrete trigger:** Make vkCreateDescriptorPool, vkCreateGraphicsPipelines, vkAllocateMemory, or vkMapMemory fail while opening Debug or name entry, for example under memory exhaustion.

**Suggested fix:** Install a contextual Vulkan-result handler. Propagate recoverable initialization/upload failures through a backend path that cleans up partial resources. Publish a working keyboard owner only after successful preparation. Route unrecoverable device errors through the existing Vulkan failure path. Test each injected failure and the visible fallback.

## 2. [P1] An old keyboard answer can answer a new ticket

**File and line:** src/nx/gpu/vk/imgui.c:89-115, especially 111-114; src/nx/runtime/platform/overlay.c:646-650.

**Fault:** The frame copies a ticket under ui_lock, releases the lock, then applies text and Enter/Cancel without checking that ticket. The overlay operations affect whichever request is current. The AM-side ticket check cannot help: overlay_keyboard_take supplies the new ticket after the wrong request was answered.

**Concrete trigger:** The frame reads request A. Before keyboard_answer takes ui_lock, the applet hides A and shows B. A's pending text or Cancel is applied to B. State loading can also replace an inline keyboard in this interval.

**Suggested fix:** Apply expected ticket, text, and answer in one overlay operation under ui_lock. Reject a response unless its ticket is still shown. Keep the later AM call outside ui_lock. Test replacement of A by B between snapshot and response for text, Enter, and Cancel.

## 3. [P2] The character filter never counts the current line

**File and line:** src/nx/gpu/vk/imgui_ui.cpp:208-213 and 248-267; src/nx/runtime/platform/overlay.c:646-650.

**Fault:** ImGui 1.93.0 does not supply Buf or BufTextLen for CallbackCharFilter. The bundled InputTextFilterCharacter initializes those fields to zero. Therefore utf16_units(NULL, 0) returns zero for every character. The field accepts text beyond k.max. The overlay truncates it, but the field retains the longer line and uses that line for display and acceptance.

**Concrete trigger:** With maximum 6, type or paste ABCDEFG. The field can show seven characters while the submitted overlay text is ABCDEF.

**Suggested fix:** Enforce limits using an editing callback that supplies the buffer, or explicit edit state. Account for selection replacement, multiple characters in a paste, UTF-16 pairs, and undo/redo. Keep the field equal to the accepted overlay text. Test the maximum boundary with ASCII, Japanese, paste, and replacement selections.

## 4. [P2] Controller confirmation overtakes queued text

**File and line:** src/nx/runtime/platform/backend/sdl3.c:1498 and 1664; src/nx/gpu/vk/imgui.c:139-153.

**Fault:** Text waits in the renderer's event queue, while controller A answers the overlay immediately on the main thread through overlay_controls. The final commit need not have reached the overlay when A submits it.

**Concrete trigger:** Deliver the final SDL_TEXT_INPUT commit and controller A in one poll before another ImGui frame. A submits the previous line, and the pending character is discarded when the keyboard closes. If that character reaches the minimum, A can instead appear to do nothing.

**Suggested fix:** Sequence external-keyboard controller confirmation with queued edits. Process it after preceding text on the context owner, then publish a ticket-checked response. Do not add a fixed delay. Test keyboard and IME commits immediately followed by A and B.

## 5. [P2] A new ticket reuses the previous active field state

**File and line:** src/nx/gpu/vk/imgui_ui.cpp:226-248.

**Fault:** A fresh ticket copies new text into line, but every request has the same Keyboard/##line widget ID. An already active InputText owns an internal copy and ignores replacement of the caller's buffer. SetKeyboardFocusHere does not force a reload of an already active field. Old text can overwrite the new initial text. Static was_shown also survives context recreation and periods when ImGui is disabled.

**Concrete trigger:** Replace an active request with a different ticket and initial text between two frames, without a hidden frame. Alternatively, disable ImGui, edit the same ticket in the legacy box, then enable it again. The old local line is retained.

**Suggested fix:** Give each ticket a distinct widget identity. Reset editing/focus state on context and owner transitions, and reload authoritative text on handoff even when the ticket is unchanged. Test ticket replacement, disable/re-enable, and swapchain recreation during editing.

## 6. [P2] Independent SDL text-input owners can stop the active keyboard

**File and line:** src/nx/runtime/platform/backend/imgui_sdl.c:95-111; src/nx/runtime/platform/backend/sdl3.c:1462-1477, 1583, and 1720.

**Fault:** g_text_on and g_typing.ticket independently represent ownership of one SDL text-input session. Their start/stop operations run in different phases of a poll. One owner can start text input, then the other stops it on the next poll. An unchanged ticket does not restart it.

**Concrete trigger:** ImGui is editing with g_text_on=1. Disable it and let keyboard_sync start the legacy keyboard before a rendered frame clears the old wants. On the next poll, imgui_sdl_text_input sees the legacy ticket, changes its own state to off, and calls SDL_StopTextInput. The legacy ticket is unchanged, so text input stays off.

**Suggested fix:** Use one main-thread state machine to choose none, legacy keyboard, or ImGui, then apply properties, IME area, and one start/stop transition. Report SDL errors with context. Test both handoff directions while typing and composing, including transitions without a rendered frame.

## 7. [P2] Queue overflow loses releases and focus loss

**File and line:** src/nx/gpu/vk/imgui_input.c:6-18.

**Fault:** All events are silently discarded when the 256 slots are full, including key-up, mouse-up, and focus loss. ImGui can retain a held key, modifier, or button indefinitely. The capacity comment assumes short frames, which shader work, resizing, debugging, and high-rate mice can exceed.

**Concrete trigger:** After ImGui receives a button-down, stall its frame, fill the queue with motion, then release the button or lose focus. The reset is dropped and the widget stays active after draining.

**Suggested fix:** Coalesce replaceable motion and guarantee state-transition delivery. On overflow, deliver a reliable reset/resynchronization rather than a partial stream. Test every release and focus loss after deliberately filling the queue.

## 8. [P2] Capture flags outlive the UI, while a timeout changes ownership without a failure cause

**File and line:** src/nx/runtime/platform/backend/sdl3.c:1498 and 1583-1584; src/nx/runtime/platform/backend/imgui_sdl.c:69-85; src/nx/gpu/vk/imgui_input.c:34-39; src/nx/gpu/vk/imgui.c:118-136.

**Fault:** Capture and cursor release use the last wants without checking the setting, readiness, or alive state. Shutdown does not clear wants. Disabling clears them only if another vk_imgui_draw reaches its disabled branch. Conversely, overlay_menu_opens changes policy after 500 ms without a frame, with no mechanism establishing backend failure. A slow frame is not proof that ImGui stopped.

**Concrete trigger:** Leave a widget active, stop successful presentation through a lost surface or persistent acquire failure, and disable ImGui. Input can still be taken and the cursor released. During a stall longer than 500 ms, F2 can open the old menu; once rendering resumes, that same F2 path cannot close it unless legacy_menu is enabled.

**Suggested fix:** Publish explicit enabled/ready/owner state with capture. Clear wants and reconcile queued/held input on disable, shutdown, and failure. Base fallback on actual lifecycle transitions. If a watchdog is required, define its failure and recovery protocol rather than treating frame age as ownership. Test these states with no successful presents.

## 9. [P2] Bypassing ImGui for the old menu loses its releases

**File and line:** src/nx/runtime/platform/backend/sdl3.c:1498.

**Fault:** The entire ImGui gateway is skipped while the old menu or prepare screen shows. Releases and focus loss then never reach a context that already saw the corresponding presses. Sending releases to the game does not reset ImGui.

**Concrete trigger:** With legacy_menu=1, hold Shift after ImGui sees its down event, open the old menu with the controller, release Shift there, then close it. ImGui retains Shift. A held mouse button or focus loss during the bypass has the same gap.

**Suggested fix:** Maintain ImGui's input state, especially releases and focus, even when another screen owns consumption. Reset/suspend the context explicitly on owner transitions. Test modifiers, dragging, and focus loss across both bypass screens.

## 10. [P2] Consuming Alt-down breaks the cursor-release gate

**File and line:** src/nx/runtime/platform/backend/imgui_sdl.c:62-66; src/nx/runtime/platform/backend/sdl3.c:1498, 1536-1537, and 1246-1248.

**Fault:** g_alt_held is updated after ImGui consumption. An Alt-down consumed by an active widget never updates it, so native capture treats Alt as up.

**Concrete trigger:** During a Debug slider interaction or window drag, press and hold Alt. Finish the interaction and move outside the window. Once ImGui releases its capture request, mouse look can recapture the cursor although Alt is still held.

**Suggested fix:** Update physical Alt/focus state before event consumption, or derive this gate from SDL's current modifiers. Keep physical cursor-release state separate from game-key consumption. Test Alt before, during, and after widget interaction.

## 11. [P2] Long text commits are truncated inside UTF-8

**File and line:** src/nx/runtime/platform/backend/imgui_sdl.c:80-84; src/nx/gpu/vk/imgui_input.h:16.

**Fault:** Only the first 31 bytes of SDL's committed string are queued. The rest is discarded, and the cut can split a code point. Japanese IME commits can exceed this size.

**Concrete trigger:** Commit eleven copies of a three-byte Japanese character in one event, with a large enough keyboard maximum. The 33-byte string becomes ten complete characters and an incomplete eleventh, which can decode as U+FFFD.

**Suggested fix:** Preserve complete commits with owned variable-size data or UTF-8-safe chunks, together with reliable queue-overflow handling. Test commits crossing 30/31/32 bytes and multibyte boundaries.

## 12. [P2] Supplementary characters become replacement characters

**File and line:** src/nx/CMakeLists.txt:107-116 and 134-138; src/nx/gpu/vk/imgui_ui.cpp:188-213.

**Fault:** ImGui uses its default 16-bit ImWchar; no IMGUI_USE_WCHAR32 configuration is supplied. Its bundled decoder/event path replaces code points above U+FFFF with U+FFFD. Thus the explicit UTF-16-pair counting branch does not preserve such typed input. The legacy UTF-8 path preserves it and counts two units.

**Concrete trigger:** Enter or paste U+1F600 or a supplementary CJK character. The submitted character becomes U+FFFD, with one UTF-16 unit instead of two.

**Suggested fix:** Enable IMGUI_USE_WCHAR32 consistently for ImGui and all translation units that use its types, preserving the UTF-16-unit contract. Test supplementary characters alone, as initial text, and at the maximum-unit boundary.

## 13. [P2] Refused Enter leaves the name field inactive

**File and line:** src/nx/gpu/vk/imgui_ui.cpp:244-255.

**Fault:** EnterReturnsTrue uses ImGui's default deactivation on Enter. Below the minimum, this code only displays a refusal. It requests focus only for a fresh ticket. Continued typing does not return to the field, and text input can stop. Input can also reach the legacy keyboard handler, whose edits the local ImGui line can overwrite.

**Concrete trigger:** For a minimum of three, type one character, press Enter, then continue typing without clicking the field.

**Suggested fix:** Restore field focus after refusal, preserving the line and intended selection/cursor. Keep one editing owner. Test repeated refusal, continued keyboard/IME input, eventual acceptance, and Escape after refusal.

## 14. [P2] The external-keyboard hook has a C data race

**File and line:** src/nx/gpu/vk/imgui.c:47; src/nx/runtime/platform/overlay.c:625-627.

**Fault:** ready writes a plain function pointer every frame under vk_lock without ui_lock. The main thread reads it through menu/keyboard ownership checks under ui_lock. These different locks do not order the accesses. Repeatedly writing the same pointer is still a data race.

**Concrete trigger:** A rendered frame calls ready concurrently with a main-thread menu-open or keyboard-sync check.

**Suggested fix:** Register the immutable hook before concurrent use, protect both accesses with one lock, or publish it atomically. Do not acquire vk_lock while holding ui_lock. Verify with a concurrent host test and race detector.

## 15. [P2] ImGui is missing from the frontend refresh predicate

**File and line:** src/nx/runtime/platform/backend/sdl3.c:1765-1770; src/nx/gpu/vk/imgui_ui.cpp:282-284.

**Fault:** Refresh is requested for the old overlays and pause, but not for ImGui windows or queued input. ImGui alone cannot keep updating when the game stops presenting.

**Concrete trigger:** Turn off on-screen controls, showcase, and legacy stats. Open Debug during a loading interval that stops guest presents without setting runtime pause. Even with the GPU free, the window and its input freeze until another guest present. Moving to the top edge cannot open the bar during that interval either.

**Suggested fix:** Include ImGui enabled/input/draw demand in the frontend refresh policy, retaining the lock order and refresh pacing. Do not depend only on an expiring alive stamp to restart it. Run this scene with no other overlay requesting refresh.

## 16. [P2] NVML symbol loading violates strict aliasing and function-type compatibility

**File and line:** src/nx/gpu/vk/debug_stats.c:123-147.

**Fault:** The expression *(void**)&n->member accesses a function-pointer object through an unrelated object-pointer lvalue, violating ScarletNX's strict-aliasing rule. The declarations also replace NVML's opaque device-pointer, enum, and structure types with void*, int, and separately declared structures. Equal storage sizes on Windows x64 do not make these C function types compatible.

**Concrete trigger:** Successfully load and call NVML in an optimized strict-aliasing build. The source violates the rule even if the current ABI tolerates it.

**Suggested fix:** Use exact ABI declarations from an appropriately licensed declaration source. Load symbols through temporaries and the established memcpy conversion pattern, with size checks. Do not disable strict aliasing. See [NVIDIA's declarations](https://raw.githubusercontent.com/NVIDIA/go-nvml/main/pkg/nvml/nvml.h). The existing milliwatt-to-watt, byte-to-MiB, and MHz conversions themselves match the [NVML query documentation](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html).

## 17. [P2] NVML can report an unrelated GPU

**File and line:** src/nx/gpu/vk/debug_stats.c:159-169.

**Fault:** The first NVML device is retained before any name match. No match is treated as success. On a hybrid machine, NVIDIA readings can be reported for an AMD or Intel Vulkan renderer. Names also cannot distinguish two devices of the same model. The first-device special case has no cause establishing it as the renderer.

**Concrete trigger:** Render on Intel or AMD while an NVIDIA GPU/NVML is installed, or select the second of two NVIDIA cards with the same model name.

**Suggested fix:** Match a stable shared identifier such as PCI identity or UUID. Report unknown when the rendering device cannot be matched. Check hybrid and multi-GPU machines while changing the selected Vulkan device.

## 18. [P2] Focused regression tests and edge-case scenes are missing

**File and line:** src/nx/CMakeLists.txt:107-116; tests/CMakeLists.txt:745-754; project docs/imgui-plan.md, Progress and Verification.

**Fault:** Patches 0013-0019 add no tests. Existing overlay/AM tests do not execute the new context, queue, SDL gateway, or NVML loader. Successful font display and basic name entry do not verify ownership transitions, length limits, input pairing, or presentation recovery. A build and unrelated shader/settings tests cannot verify those behaviors.

**Concrete trigger:** The existing tests can pass with findings 2-14 still present. The recorded basic name-entry scene does not cover minimum refusal, maximum paste, or controller ordering.

**Suggested fix:** Add host tests for the failure/interleaving cases above. After fixes, have the user run these small scenes:

- Open and close Debug during a cutscene, battle, and game menu. Repeat during an interval with no guest presents.
- Hold game keys and mouse buttons while moving into/out of ImGui. Test Alt during interaction, key-up over windows, focus loss, and restoration.
- Test F2, Back, controller Home, and the on-screen menu button with legacy_menu both off and on, including recovery after a long rendering stall. The scripted Home exception must be explicit.
- During name entry, test Japanese IME, long commits, minimum refusal, maximum paste, selection replacement, controller accept/cancel, Escape, ticket replacement, and ImGui disable/re-enable.
- With Debug and then the keyboard open, resize, minimize/restore, switch fullscreen, and recreate the swapchain through V-sync/image-count and sRGB/format changes. Run with recording on/off and synchronization validation enabled.
- Check missing-font/backend-failure fallback and missing NVML, plus an AMD/Intel or hybrid GPU machine.

Read logs after user runs and record the actual scenes. Compare the three recorded test failures with an unchanged baseline before calling them unrelated. Do not claim runtime verification from a build.

## 19. [P3] Failed NVML loading loses context and retains partial resources

**File and line:** src/nx/gpu/vk/debug_stats.c:141-158, 169, and 228-229.

**Fault:** Once a library opens, missing-symbol and initialization/enumeration failures return without closing it. After successful initialization, later failure has no shutdown path. The local library handle is discarded. All causes become the same message that there is no library, including missing exports and driver initialization errors. This violates the explicit ownership and error-context rules.

**Concrete trigger:** A library lacks an export, nvmlInit_v2 fails, or enumeration fails after successful initialization.

**Suggested fix:** Retain library/initialization ownership. Unwind partial resources on failure and report path, symbol/operation, and result. Keep the supported missing-library fallback to unknown values. Test each failure stage with a fake loader.

## 20. [P3] Hardware polling adds an unexplained grace period and implicit retries

**File and line:** src/nx/gpu/vk/debug_stats.c:24-25, 234-245, and 249-256.

**Fault:** Polling continues for three seconds after Debug stops asking, although the design says to poll while open. No cause explains this grace period. If thread creation fails, every subsequent visible frame silently retries it. There is no reported cause, failed state, or recovery event. These are a delay and retry without a stated mechanism.

**Concrete trigger:** Close Debug just after a poll and observe continued reads. Or force plat_thread to fail and keep Debug rendering.

**Suggested fix:** Publish explicit hardware demand on open/close. Report thread-start failure and define a recovery event if retry is needed. Retain a grace period only with a documented cause. Test post-close poll counts and repeated thread-start attempts.

## 21. [P3] A new code comment exceeds four lines

**File and line:** src/nx/gpu/vk/imgui.c:3-7.

**Fault and trigger:** The submitted opening comment has five lines. The Code rule permits at most four.

**Suggested fix:** Keep a short recording/lock requirement in source and move the fuller explanation into renderer documentation or the plan.

## 22. [P3] Theme classification thresholds are unnamed

**File and line:** src/nx/gpu/vk/imgui_style.cpp:18.

**Fault and trigger:** Applying the theme classifies colors with the meaningful thresholds 0.4f, 0.2f, and 0.15f directly. The Code rule requires named constants for meaningful thresholds/settings.

**Suggested fix:** Name the minimum blue channel and blue-over-red/green differences, and state the classification assumption.

## 23. [P3] Ordinary struct clearing bypasses required initialization

**File and line:** src/nx/gpu/vk/imgui.c:138; src/nx/gpu/vk/debug_stats.c:78, 212, and 227.

**Fault:** These memset calls clear ordinary structs whose padding is not hashed, compared, or serialized. ScarletNX explicitly requires language-level zero initialization/assignment for this case. Nvml also contains pointers, whose intended null values should be expressed as such.

**Concrete trigger:** Frame preparation, statistics reads, and hardware-thread startup execute these resets.

**Suggested fix:** Use initializers or compound-literal assignment. Keep byte clearing only where representation/padding matters.
