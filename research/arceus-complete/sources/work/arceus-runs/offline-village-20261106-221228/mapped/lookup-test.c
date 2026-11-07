#include "mem.c"
#include <assert.h>
int main(void) {
    static MemBlock blocks[260];
    assert(mem_mapped_run(123, 9) == 9 && mem_mapped_run(123, 0) == 0);
    const u64 unit = 1u << 20;
    g_blk = blocks; g_nblk = 260;
    for (unsigned j=0; j<260; j++) {
        blocks[j].addr = j*unit;
        blocks[j].size = unit;
        blocks[j].state = MS_NORMAL;
    }
    blocks[259].size = AS_END-blocks[259].addr;
    for (unsigned pass=0; pass<12; pass++) {
        mem_lock(); runs_stale_nl();
        for (unsigned j=0;j<259;j++)
            blocks[j].state=(j+pass)%7==0?MS_FREE:(j+pass)%13==0?MS_INACCESSIBLE:MS_NORMAL;
        mem_unlock();
        for(unsigned j=0;j<259;j++) {
            unsigned k=j;
            while(k<260 && blocks[k].state!=MS_FREE && blocks[k].state!=MS_INACCESSIBLE) k++;
            u64 addr=(j+1)*unit-3;
            u64 available=k==j?0:(k==260?AS_END:k*unit)-addr;
            const u64 sizes[]={0,1,3,4,unit,UINT64_MAX};
            for(unsigned n=0;n<sizeof sizes/sizeof *sizes;n++) {
                u64 expect=sizes[n]<available?sizes[n]:available;
                assert(mem_mapped_run(addr,sizes[n])==expect);
            }
        }
        mem_lock();
        assert(mem_mapped_run(AS_END-1,8)==1 && mem_mapped_run(AS_END,1)==0);
        mem_unlock();
    }
    puts("Mapped-run boundaries, cache collisions, held lock, holes and invalidations passed.");
    return 0;
}
