#include "C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/memory/mem.c"

extern u64 run_lookups(unsigned count);
int main(void) {
    static MemBlock block;
    block.size=AS_END;block.state=MS_NORMAL;g_blk=&block;g_nblk=1;
    const unsigned count=32000000;
    LARGE_INTEGER freq, start, stop;
    QueryPerformanceFrequency(&freq);
    (void)run_lookups(1000000);
    QueryPerformanceCounter(&start);
    u64 sum=run_lookups(count);
    QueryPerformanceCounter(&stop);
    if(sum!=(u64)(count/8)*(4096+7)) return 1;
    printf("%.5f\n",1e9*(stop.QuadPart-start.QuadPart)/freq.QuadPart/count);
    return 0;
}
