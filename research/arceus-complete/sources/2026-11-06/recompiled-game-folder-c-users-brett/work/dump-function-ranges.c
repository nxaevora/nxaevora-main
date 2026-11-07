#include <windows.h>
#include <stdio.h>
#include "nx/loader/module_abi.h"
int main(void) {
 HMODULE dll=LoadLibraryA("C:\\Users\\brett\\Documents\\Codex\\work\\arceus-games\\ArceusTest\\ArceusTest_code.dll");
 if(!dll)return 1;
 const SnxModule*(*get)(void)=(const SnxModule*(*)(void))GetProcAddress(dll,"snx_module");
 if(!get)return 2;
 const SnxModule*m=get();
 FILE*f=fopen("work/functions.tsv","w");if(!f)return 3;
 for(u32 i=0;i<m->nfns;i++)fprintf(f,"%llx\t%u\n",(unsigned long long)m->fn_lo[i],m->fn_len[i]);
 fclose(f);return 0;
}
