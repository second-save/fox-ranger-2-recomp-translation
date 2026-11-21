/* test_pklite - unpack every packed EXE and compare with the emulator-made copy.
 * usage: test_pklite PACKED_DIR PRISTINE_DIR NAME.EXE...   (e.g. work/extracted work/pristine) */
#include "pklite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* Same program as the reference: entry, stack, image bytes and relocations
 * within our image.  The reference may continue past our image with the
 * stub's leftover packed bytes (see pklite.h); *tail = how many. */
static int same_program(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, size_t *tail)
{
    for (int f = 0x0E; f <= 0x16; f += 2) if (f != 0x12 && u16(a + f) != u16(b + f)) return 0;   /* SS SP IP CS */
    size_t ah = u16(a + 8) * 16u, bh = u16(b + 8) * 16u, al = an - ah, bl = bn - bh;
    while (al && !a[ah + al - 1]) al--;                    /* our zero padding may be their leftovers */
    if (al > bl) return 0;
    if (memcmp(a + ah, b + bh, al)) return 0;
    *tail = bl - al;
    size_t na = u16(a + 6), nb = u16(b + 6), j = 0;
    for (size_t i = 0; i < nb; i++) {
        size_t r = u16(b + 0x1E + 4 * i) * 16u + u16(b + 0x1C + 4 * i);
        if (r + 2 > al) continue;                                  /* inside the leftover tail */
        if (j >= na || u16(a + 0x1E + 4 * j) * 16u + u16(a + 0x1C + 4 * j) != r) return 0;
        j++;
    }
    return j == na;
}

static uint8_t *slurp(const char *dir, const char *name, size_t *n)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*n);
    if (d && fread(d, 1, *n, f) != *n) { free(d); d = NULL; }
    fclose(f);
    return d;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s PACKED_DIR PRISTINE_DIR NAME.EXE...\n", argv[0]); return 2; }
    int fails = 0;
    for (int i = 3; i < argc; i++) {
        size_t pn, rn, on;
        uint8_t *packed = slurp(argv[1], argv[i], &pn), *ref = slurp(argv[2], argv[i], &rn), *out = NULL;
        int r = packed ? exe_unpack(packed, pn, &out, &on) : -2;
        size_t tail = 0;
        int same = r == 0 && ref && same_program(out, on, ref, rn, &tail);
        printf("%-10s %s", argv[i], same ? "ok" : "FAIL");
        if (same && tail) printf("  (reference adds %zu bytes of stub leftovers)", tail);
        if (!same) printf("  (rc %d, %zu vs %zu bytes)", r, r == 0 ? on : 0, ref ? rn : 0);
        printf("\n");
        fails += !same;
        if (out && getenv("PKLITE_OUT")) {           /* keep outputs for inspection */
            char op[1024];
            snprintf(op, sizeof op, "%s/%s", getenv("PKLITE_OUT"), argv[i]);
            FILE *f = fopen(op, "wb");
            if (f) { fwrite(out, 1, on, f); fclose(f); }
        }
        free(packed); free(ref); free(out);
    }
    return fails != 0;
}
