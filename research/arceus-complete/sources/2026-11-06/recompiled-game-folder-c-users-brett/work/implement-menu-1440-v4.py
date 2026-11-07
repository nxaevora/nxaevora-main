from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c');s=p.read_text();Path('work/arceus-clock-before-v4.c').write_text(s)
code=r'''/* This collector updates held/pressed/released masks every render. Only its
   autorepeat countdown is authored in 30-Hz ticks. Do not throttle the collector
   itself: that would lose short presses and slow analog movement/camera input. */
static GuestFn pad_repeat_original;
static u64 pad_repeat_calls;
static void pad_repeat_update(CPU* c, u64 entry) {
    u64 object = c->x[0], return_pc = c->x[30];
    pm_lock(&step_lock);
    unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    pad_repeat_original(c, entry);
    if (c->pc != return_pc || !readable(object, 0x104)) return;
    u32 counter = gr32(object + 0x18);
    u64 repeat = gr64(object + 0xb8);
    if (!ticks) {
        /* Undo exactly the one decrement executed by the original collector,
           including a countdown it just initialized after a direction change.
           +A8 is the separate fresh-press mask and remains untouched. */
        gw32(object + 0x18, counter + 1);
        gw64(object + 0xb8, 0);
    }
    /* At most one repeat per render even after a stall: no catch-up burst. */
    u64 count = __atomic_add_fetch(&pad_repeat_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 600 == 0)
        LOG("arceus-pad-repeat: count=%llu ticks=%u object=%llx initial=%u interval=%u counter=%u pressed=%llx repeat_before=%llx repeat_after=%llx\n",
            (unsigned long long)count, ticks, (unsigned long long)object,
            gr32(object+0x10), gr32(object+0x14), gr32(object+0x18),
            (unsigned long long)gr64(object+0xa8), (unsigned long long)repeat,
            (unsigned long long)gr64(object+0xb8));
}
static void pad_repeat_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_MENU_REPEAT_FIX");
    if (!enabled || strcmp(enabled,"1")) return;
    u64 pc = main_base + 0xc9a9b4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i=0; i<g_mod->nfns; i++)
        if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==0x8e8) covered=1;
    if (!covered || pc<g_code_lo || pc+0x8e8>g_code_hi || !readable(pc,0x8e8) ||
        gr32(pc)!=0x6db923e9 || gr32(main_base+0xc9ac44)!=0x51000530 ||
        gr32(pc+0x8e4)!=0x17ffffea) {
        WARN("arceus-pad-repeat: refused instruction/function guard\n"); return;
    }
    gw32(pc+0x8e4,0xb4fffd5f); /* Equivalent final B -> CBZ XZR. */
    jit_code_changed(pc,0x8e8);
    pad_repeat_original=jit_wait(pc);
    if (!pad_repeat_original) { WARN("arceus-pad-repeat: compile failed\n"); return; }
    __atomic_store_n(&g_disp[(pc-g_code_lo)/4],pad_repeat_update,__ATOMIC_RELEASE);
    LOG("arceus-pad-repeat: 30-Hz autorepeat countdown installed; fresh presses unchanged\n");
}

'''
s=s.replace('static GuestFn field_message_original;',code+'static GuestFn field_message_original;').replace('    blink_fix_init();','    blink_fix_init();\n    pad_repeat_init();');p.write_text(s)
p=Path('outputs/Capture-Arceus-Timing.ps1');s=p.read_text();s=s.replace('[switch]$CameraInputFix,','[switch]$CameraInputFix, [switch]$MenuRepeatFix, [ValidateRange(0,16)][double]$ResolutionScale=0, [switch]$Fullscreen,');s=s.replace('$env:SNX_ARCEUS_CAMERA_SPEED=', '''if ($ResolutionScale -gt 0 -and $ResolutionScale -lt 0.25) { throw 'ResolutionScale must be 0 (saved setting) or at least 0.25.' }
$env:SNX_ARCEUS_MENU_REPEAT_FIX=if ($MenuRepeatFix) { '1' } else { '0' }
if ($MenuRepeatFix -and !$TimingFixes) { throw 'Menu repeat fix requires TimingFixes.' }
$env:SNX_ARCEUS_CAMERA_SPEED=''')
old='''$game=Start-Process -FilePath "$gameDir\\ArceusTest.exe" -ArgumentList @('--nvn_native=0','--fps_limit=game','--fullscreen=0','--log=1',"--seconds=$Seconds")'''
new='''$gameArgs=@('--nvn_native=0','--fps_limit=game',('--fullscreen='+[int]$Fullscreen),'--log=1',"--seconds=$Seconds")
if ($ResolutionScale -gt 0) { $gameArgs+=('--resolution='+$ResolutionScale.ToString([Globalization.CultureInfo]::InvariantCulture)) }
@{target_fps=$ExperimentalFps;camera_speed=$CameraSpeed;menu_repeat=[bool]$MenuRepeatFix;resolution_scale_override=$ResolutionScale;fullscreen=[bool]$Fullscreen;runtime_sha256=(Get-FileHash "$gameDir/ArceusTest.exe").Hash} | ConvertTo-Json | Set-Content "$runDir/test-settings.json"
$game=Start-Process -FilePath "$gameDir\\ArceusTest.exe" -ArgumentList $gameArgs'''
assert old in s;s=s.replace(old,new);p.write_text(s)
for fps in [60,120]:
 p=Path(f'outputs/Test-Arceus-Responsive-Camera-{fps}FPS.cmd');s=p.read_text().replace('-CameraSpeed 2 ', '-CameraSpeed 2.2 -MenuRepeatFix ');p.write_text(s)
 Path(f'outputs/Test-Arceus-1440p-{fps}FPS.cmd').write_text(s.replace('-CaptureDisplay','-ResolutionScale 1.3333333333333333 -Fullscreen -CaptureDisplay'))
