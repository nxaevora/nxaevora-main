#include "nx/memory/mem.h"
u64 run_lookups(unsigned count);
u64 run_lookups(unsigned count) {
    u64 sum = 0;
    for (unsigned n=0;n<count;n++) {
        u64 address = 0x1000 + ((n * 4099u) & 0x3ffffffu);
        sum += mem_mapped_run(address, (n & 7u) ? 1 : 4096);
    }
    return sum;
}
