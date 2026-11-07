#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
/* Standalone test never enables the game's sampling profiler. */
void plat_prof_add_thread(HANDLE h) { (void)h; abort(); }
