from pathlib import Path
root=Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
p=root/'src/nx/runtime/arceus_clock.c';s=p.read_text()
s=s.replace('#include "nx/runtime/loader.h"','#include "nx/runtime/loader.h"\n#include "nx/runtime/options.h"\n#include "nx/runtime/arceus_step.h"')
s=s.replace('static u64 main_base;','''static u64 main_base;
static int experimental, tracing;
static ArceusStep step;
static PMutex step_lock = PMUTEX_INIT;

static void adjust_duration(CPU* c) {
    static const u64 callers[4] = {0x278150c, 0x2781540, 0x2781574, 0x27815a8};
    unsigned channel = (unsigned)c->x[1];
    u64 value;
    if (!experimental || c->x[1] > 3 || c->x[30] != main_base + callers[channel]) return;
    if (c->x[2] != 33333334) {
        WARN_LIMIT(4, "arceus-clock: unexpected main-loop duration %llu, left unchanged\\n", (unsigned long long)c->x[2]);
        return;
    }
    pm_lock(&step_lock);
    if (arceus_step(&step, plat_ns(), c->x[0], channel, &value)) c->x[2] = value;
    else WARN_LIMIT(4, "arceus-clock: clock update order changed, left unchanged\\n");
    pm_unlock(&step_lock);
}''')
s=s.replace('    originals[kind](c, entry);','    if (kind == 2) adjust_duration(c);\n    originals[kind](c, entry);',1)
s=s.replace('    if (count <= 80 || count % 600 == 0) {','    if (tracing && (count <= 40 || count % (kind == 0 ? 60000 : 600) == 0)) {',1)
s=s.replace('/* This probe observes virtual calls only. It leaves all game state and pacing unchanged.\n   Refuse other builds or changed instructions before installing any dispatch entry. */','/* The default probe only observes virtual calls. The optional experiment changes the four verified\n   main-loop duration writes. Build and instruction guards precede both hooks and the 60 FPS pacing override. */')
s=s.replace('    const SnxModInfo* mod = NULL;\n    if (!enabled || strcmp(enabled, "1")) return;', '''    const char* fps60 = getenv("SNX_ARCEUS_FPS60");
    const SnxModInfo* mod = NULL;
    tracing = enabled && !strcmp(enabled, "1");
    experimental = fps60 && !strcmp(fps60, "1");
    if (!tracing && !experimental) return;''')
s=s.replace('    LOG("arceus-clock: installed read-only clock probe for Arceus 1.1.1\\n");','''    if (experimental) __atomic_store_n(&g_opt_fps, 60, __ATOMIC_RELAXED);
    LOG("arceus-clock: Arceus 1.1.1 %s, tracing=%d\\n",
        experimental ? "EXPERIMENT: elapsed-time clocks with 60 FPS cap" : "read-only probe", tracing);''')
p.write_text(s)
for r in [root,Path(r'C:/Users/brett/Documents/Codex/work/nvn-src')]:
 (r/'src/nx/runtime/arceus_step.h').write_text(Path('work/arceus_step.h').read_text())
 (r/'src/nx/runtime/arceus_clock.c').write_text(s)
