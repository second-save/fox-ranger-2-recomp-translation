/* pklite.c - native PKLITE 1.12 unpacker; format as documented by Deark
 * (modules/pklite.c).  Only the variants used by Fox Ranger II are handled:
 * v1.12, small or large compression, with or without "extra" compression. */
#include "pklite.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *p, *end; unsigned buf; int nb, err; } Bits;

static unsigned byte(Bits *b)
{
    if (b->p >= b->end) { b->err = 1; return 0; }
    return *b->p++;
}
static void refill(Bits *b) { b->buf = byte(b); b->buf |= byte(b) << 8; b->nb = 16; }
static int bit(Bits *b)
{
    int v = b->buf & 1;                /* LSB first; refill as soon as the word is used up */
    b->buf >>= 1;
    if (--b->nb == 0) refill(b);
    return v;
}

/* prefix codes, read first bit first; symbol = index */
typedef struct { const char *code; } Code;
static const Code LEN_SM[] = { {"00"}, {"100"}, {"101"}, {"1100"}, {"1101"}, {"1110"}, {"1111"}, {"011"}, {"010"}, {0} };
static const Code LEN_LG[] = { {"11"}, {"000"}, {"0010"}, {"0011"}, {"0100"}, {"01010"}, {"01011"}, {"01100"},
    {"011010"}, {"011011"}, {"0111010"}, {"0111011"}, {"0111100"}, {"01111010"}, {"01111011"}, {"01111100"},
    {"011111010"}, {"011111011"}, {"011111100"}, {"011111101"}, {"011111110"}, {"011111111"}, {"011100"}, {"10"}, {0} };
static const Code OFFS[] = { {"1"}, {"0000"}, {"0001"}, {"00100"}, {"00101"}, {"00110"}, {"00111"}, {"010000"},
    {"010001"}, {"010010"}, {"010011"}, {"010100"}, {"010101"}, {"010110"}, {"0101110"}, {"0101111"},
    {"0110000"}, {"0110001"}, {"0110010"}, {"0110011"}, {"0110100"}, {"0110101"}, {"0110110"}, {"0110111"},
    {"0111000"}, {"0111001"}, {"0111010"}, {"0111011"}, {"0111100"}, {"0111101"}, {"0111110"}, {"0111111"}, {0} };

static int decode(Bits *b, const Code *t)
{
    char acc[16];
    for (int n = 0; n < 15; n++) {
        acc[n] = (char)('0' + bit(b));
        acc[n + 1] = 0;
        for (int i = 0; t[i].code; i++)
            if (!strcmp(t[i].code, acc)) return i;
    }
    b->err = 1;
    return -1;
}

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* MZ file laid out like tools/unpack_exe.py: image trimmed of trailing zeros,
 * sorted relocations, min-alloc covering the stack plus the packed file's */
static int build_mz(const uint8_t *img, size_t len, uint32_t *rel, size_t nrel, unsigned ss, unsigned sp,
                    unsigned cs, unsigned ip, unsigned packed_minalloc, uint8_t **out, size_t *outn)
{
    qsort(rel, nrel, sizeof *rel, cmp_u32);
    size_t img_end = len;
    while (img_end && !img[img_end - 1]) img_end--;
    img_end = (img_end + 15) & ~(size_t)15;
    size_t hsize = (0x1C + 4 * nrel + 15) / 16 * 16, total = hsize + img_end;
    long stack_top = (long)(int16_t)ss * 16 + sp;
    long min_extra = (stack_top - (long)img_end + 15) / 16;
    if (min_extra < 0) min_extra = 0;
    min_extra += 0x10;
    unsigned long minalloc = (unsigned long)min_extra + packed_minalloc;
    uint8_t *o = calloc(total, 1);
    if (!o) return -1;
    o[0] = 'M'; o[1] = 'Z';
    put16(o + 2, (unsigned)(total % 512)); put16(o + 4, (unsigned)((total + 511) / 512));
    put16(o + 6, (unsigned)nrel); put16(o + 8, (unsigned)(hsize / 16));
    put16(o + 0x0A, minalloc > 0xFFFF ? 0xFFFF : (unsigned)minalloc); put16(o + 0x0C, 0xFFFF);
    put16(o + 0x0E, ss); put16(o + 0x10, sp); put16(o + 0x14, ip); put16(o + 0x16, cs); put16(o + 0x18, 0x1C);
    for (size_t i = 0; i < nrel; i++) {
        put16(o + 0x1C + 4 * i, rel[i] & 15);
        put16(o + 0x1E + 4 * i, rel[i] >> 4);
    }
    memcpy(o + hsize, img, img_end);
    *out = o;
    *outn = total;
    return 0;
}

static int pklite_unpack(const uint8_t *in, size_t n, uint8_t **out, size_t *outn)
{
    if (n < 0x40 || in[0] != 'M' || in[1] != 'Z') return 1;
    unsigned ver = u16(in + 0x1C);
    if ((ver & 0x0FFF) != 0x10C) return 1;
    int large = (ver & 0x2000) != 0, extra = (ver & 0x1000) != 0;
    size_t hdr = u16(in + 8) * 16u, last = u16(in + 2), pages = u16(in + 4);
    size_t end = pages * 512 - (last ? 512 - last : 0);       /* end of DOS code */
    if (end > n || hdr >= end) return -1;

    /* decompressor stub: "std; mov bx,ds; push bx; add bx, N" -> data at paragraph N
     * of the stub's own segment (the entry point, ip 0100h, is the image start) */
    size_t data = 0;
    for (size_t i = hdr; i + 8 < end && i < hdr + 1000; i++)
        if (!memcmp(in + i, "\xfd\x8c\xdb\x53\x83\xc3", 6)) { data = hdr + in[i + 6] * 16u - 0x100; break; }
    if (!data || data >= end) return -1;

    size_t cap = 0x40000, len = 0;
    uint8_t *img = calloc(cap, 1);
    if (!img) return -1;
    Bits b = { in + data, in + end, 0, 0, 0 };
    refill(&b);
    const Code *lt = large ? LEN_LG : LEN_SM;
    int long_sym = large ? 22 : 7, ml2_sym = large ? 23 : 8, bias = large ? 25 : 10;
    for (;;) {
        if (b.err || len + 300 > cap) goto bad;
        if (!bit(&b)) {
            unsigned c = byte(&b);
            if (extra) c ^= (unsigned)b.nb;                    /* bits left after the flag (and refill) */
            img[len++] = (uint8_t)c;
            continue;
        }
        int sym = decode(&b, lt), mlen, hi = 0;
        if (sym < 0) goto bad;
        if (sym == ml2_sym) mlen = 2;
        else if (sym == long_sym) {
            unsigned x = byte(&b);
            if (x == 0xFF) break;                              /* end of data */
            if (x == 0xFE && large) continue;                  /* segment break: no-op */
            if (x >= 0xFD) goto bad;                           /* uncompressed areas: not used by FR2 */
            mlen = (int)x + bias;
            hi = decode(&b, OFFS);
        } else {
            mlen = sym + 3;
            hi = decode(&b, OFFS);
        }
        if (hi < 0) goto bad;
        size_t dist = (size_t)hi << 8 | byte(&b);
        if (!dist || dist > len) goto bad;
        for (int k = 0; k < mlen; k++, len++) img[len] = img[len - dist];
    }

    /* relocation table, then SS SP CS IP */
    const uint8_t *p = b.p;
    size_t nrel = 0, relcap = 1024;
    uint32_t *rel = malloc(relcap * sizeof *rel);
    if (!rel) goto bad;
    if (extra) {
        for (unsigned seg = 0;; seg += 0x0FFF) {
            if (p + 2 > in + end) goto bad_rel;
            unsigned cnt = u16(p); p += 2;
            if (cnt == 0xFFFF) break;
            for (unsigned k = 0; k < cnt; k++, p += 2) {
                if (p + 2 > in + end) goto bad_rel;
                if (nrel == relcap && !(rel = realloc(rel, (relcap *= 2) * sizeof *rel))) goto bad;
                rel[nrel++] = seg * 16u + u16(p);
            }
        }
    } else {
        for (;;) {
            if (p + 1 > in + end) goto bad_rel;
            unsigned cnt = *p++;
            if (!cnt) break;
            if (p + 2 + 2 * cnt > in + end) goto bad_rel;
            unsigned seg = u16(p); p += 2;
            for (unsigned k = 0; k < cnt; k++, p += 2) {
                if (nrel == relcap && !(rel = realloc(rel, (relcap *= 2) * sizeof *rel))) goto bad;
                rel[nrel++] = seg * 16u + u16(p);
            }
        }
    }
    if (p + 8 > in + end) goto bad_rel;
    unsigned ss = u16(p), sp = u16(p + 2), cs = u16(p + 4), ip = u16(p + 6);
    int r = build_mz(img, len, rel, nrel, ss, sp, cs, ip, u16(in + 0x0A), out, outn);
    free(rel);
    free(img);
    return r;
bad_rel:
    free(rel);
bad:
    free(img);
    return -1;
}

/* Microsoft EXEPACK: header "ip cs mem_start size sp ss dest_len 'RB'" at the
 * stub's CS:0000 (ip = 0010h); commands are read backwards from the end of
 * the packed data: B0 = fill (len, byte), B2 = copy (len bytes); bit 0 ends.
 * The relocation table follows the stub's "Packed file is corrupt" message:
 * 16 x (count, count offsets), one per 64K slice. */
static int exepack_unpack(const uint8_t *in, size_t n, uint8_t **out, size_t *outn)
{
    if (n < 0x40 || in[0] != 'M' || in[1] != 'Z') return 1;
    size_t hdr = u16(in + 8) * 16u, last = u16(in + 2), pages = u16(in + 4);
    size_t end = pages * 512 - (last ? 512 - last : 0);
    unsigned cs0 = u16(in + 0x16), ip0 = u16(in + 0x14);
    size_t eh = hdr + cs0 * 16u;                          /* EXEPACK header */
    if (end > n || ip0 != 0x10 || eh + 16 > end || u16(in + eh + 14) != 0x4252) return 1;
    unsigned rip = u16(in + eh), rcs = u16(in + eh + 2), rsp = u16(in + eh + 8), rss = u16(in + eh + 10);
    size_t dlen = u16(in + eh + 12) * 16u, plen = cs0 * 16u;
    if (dlen < plen) return -1;
    uint8_t *img = calloc(dlen ? dlen : 1, 1);
    if (!img) return -1;
    memcpy(img, in + hdr, plen);
    size_t src = plen, dst = dlen;
    for (int k = 0; k < 15 && src && img[src - 1] == 0xFF; k++) src--;    /* padding */
    for (;;) {
        if (src < 3) goto bad;
        unsigned cmd = img[--src];
        size_t cnt = (size_t)img[src - 2] | (size_t)img[src - 1] << 8;
        src -= 2;
        if ((cmd & 0xFE) == 0xB0) {
            if (!src || cnt > dst) goto bad;
            uint8_t fill = img[--src];
            dst -= cnt;
            memset(img + dst, fill, cnt);
        } else if ((cmd & 0xFE) == 0xB2) {
            if (cnt > src || cnt > dst) goto bad;
            src -= cnt; dst -= cnt;
            memmove(img + dst, img + src, cnt);
        } else goto bad;
        if (cmd & 1) break;
    }
    static const char msg[] = "Packed file is corrupt";
    size_t m = eh;
    while (m + sizeof msg - 1 <= end && memcmp(in + m, msg, sizeof msg - 1)) m++;
    if (m + sizeof msg - 1 > end) goto bad;
    /* the previous layer trimmed trailing zeros: past the end the table reads as 0 */
    size_t q = m + sizeof msg - 1, nrel = 0, cap = 64;
#define TBL16(at) ((at) + 2 <= end ? u16(in + (at)) : 0u)
    uint32_t *rel = malloc(cap * sizeof *rel);
    if (!rel) goto bad;
    for (unsigned seg = 0; seg < 16; seg++) {
        unsigned cnt = TBL16(q); q += 2;
        if (cnt && q + 2 * (size_t)cnt > end) { free(rel); goto bad; }
        for (unsigned k = 0; k < cnt; k++, q += 2) {
            if (nrel == cap) {
                uint32_t *nr = realloc(rel, (cap *= 2) * sizeof *rel);
                if (!nr) { free(rel); goto bad; }
                rel = nr;
            }
            rel[nrel++] = seg * 0x10000u + u16(in + q);
        }
    }
#undef TBL16
    int r = build_mz(img, dlen, rel, nrel, rss, rsp, rcs, rip, u16(in + 0x0A), out, outn);
    free(rel);
    free(img);
    return r;
bad:
    free(img);
    return -1;
}

int exe_unpack(const uint8_t *in, size_t n, uint8_t **out, size_t *outn)
{
    uint8_t *cur = NULL;
    size_t curn = n;
    for (int layer = 0; layer < 4; layer++) {
        const uint8_t *src = cur ? cur : in;
        uint8_t *o;
        size_t on;
        int r = pklite_unpack(src, curn, &o, &on);
        if (r == 1) r = exepack_unpack(src, curn, &o, &on);
        if (r == 1) break;                                    /* no (more) packing */
        free(cur);
        if (r) return -1;
        cur = o; curn = on;
    }
    if (!cur) return 1;
    *out = cur;
    *outn = curn;
    return 0;
}
