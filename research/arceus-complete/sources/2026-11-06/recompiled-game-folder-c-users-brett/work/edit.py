from pathlib import Path
repo=Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main')
p=repo/'third_party/ScarletNX/src/nx/gpu/vk/record.c'
s=p.read_text(); s=s.replace('#include <stdlib.h>','#include <stdio.h>\n#include <stdlib.h>',1)
needle='static VkResult present_run(const Present* x) {'
helper='''/* Optional CPU timestamps around the real Vulkan present call, not a displayed-frame counter.
   PresentMon supplies display completion and dropped-frame evidence. Calls are serialized by the recording
   thread, or by vk_lock with recording disabled. Buffering avoids a disk flush on every frame. */
static FILE* present_trace(void) {
    static int initialized;
    static FILE* file;
    if (!initialized) {
        const char* path = getenv("SNX_PRESENT_TRACE");
        initialized = 1;
        if (path && *path) {
            file = fopen(path, "wx");
            if (file) {
                setvbuf(file, NULL, _IOFBF, 65536);
                fputs("begin_ns,end_ns,swapchain,image,result\\n", file);
            } else {
                WARN("present trace: cannot create %s (use a new filename)\\n", path);
            }
        }
    }
    return file;
}
'''
assert needle in s;s=s.replace(needle,helper+needle,1)
old='    return vkQueuePresentKHR(g_vk.gfx, &pi);'
new='''    FILE* trace = present_trace();
    u64 begin = trace ? plat_ns() : 0;
    VkResult result = vkQueuePresentKHR(g_vk.gfx, &pi);
    u64 end = trace ? plat_ns() : 0;
    if (trace && fprintf(trace, "%llu,%llu,%llu,%u,%d\\n", (unsigned long long)begin,
                         (unsigned long long)end, (unsigned long long)(uintptr_t)x->sc, x->idx, (int)result) < 0) {
        WARN_LIMIT(1, "present trace: write failed\\n");
    }
    return result;'''
assert old in s;s=s.replace(old,new,1);p.write_text(s)
build=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/vk/record.c')
assert build.read_text()==s.replace('#include <stdio.h>\n','',1).replace(helper,'',1).replace(new,old,1)
build.write_text(s)
