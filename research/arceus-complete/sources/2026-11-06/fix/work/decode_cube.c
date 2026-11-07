#include <stdio.h>
#include <stdlib.h>
#include "bcn.h"
int main(int argc,char**argv){
 if(argc!=7)return 2;
 unsigned w=atoi(argv[2]),h=atoi(argv[3]),layers=atoi(argv[4]);int kind=atoi(argv[5]);
 size_t in=((w+3)/4)*((h+3)/4)*16,out=w*h*8;
 unsigned char *a=malloc(in),*b=malloc(out);if(!a||!b)return 3;
 FILE*f=fopen(argv[1],"rb"),*g=fopen(argv[6],"wb");if(!f||!g)return 4;
 for(unsigned i=0;i<layers;i++){if(fread(a,1,in,f)!=in)return 5;bcn_decode_image(a,w,h,kind,b);if(fwrite(b,1,out,g)!=out)return 6;}
 fclose(f);fclose(g);free(a);free(b);return 0;
}
