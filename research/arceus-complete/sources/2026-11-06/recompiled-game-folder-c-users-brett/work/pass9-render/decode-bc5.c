#include <stdio.h>
#include <stdlib.h>
#include "support/bcn.h"
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    unsigned w = (unsigned)strtoul(argv[1], 0, 10), h = (unsigned)strtoul(argv[2], 0, 10);
    if (!w || !h || w > 4096 || h > 4096) return 3;
    size_t size = ((w + 3) / 4) * ((h + 3) / 4) * 16, out_size = (size_t)w * h * 4;
    unsigned char *b = malloc(size), *out = malloc(out_size);
    FILE *f = fopen(argv[3], "rb");
    if (!b || !out || !f || fread(b, 1, size, f) != size || fgetc(f) != EOF) return 4;
    fclose(f);
    bcn_decode_image(b, w, h, BCN_BC5, out);
    f = fopen(argv[4], "wb");
    if (!f || fwrite(out, 1, out_size, f) != out_size) return 5;
    fclose(f); free(out); free(b); return 0;
}
