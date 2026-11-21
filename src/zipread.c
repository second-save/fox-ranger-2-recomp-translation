/* zipread.c - PKZIP 1.x reader: stored (0), shrunk (1) and imploded (6) members.
 * Implode per PKWARE APPNOTE 5.3; trees decode like canonical Huffman codes
 * over complemented bits (as Info-ZIP's explode.c does). */
#include "zipread.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ bits */
typedef struct { const uint8_t *p, *end; unsigned buf; int nb, err; } Bits;

static int getbit(Bits *b)
{
    if (!b->nb) {
        if (b->p >= b->end) { b->err = 1; return 0; }
        b->buf = *b->p++;
        b->nb = 8;
    }
    int v = b->buf & 1;
    b->buf >>= 1;
    b->nb--;
    return v;
}
static unsigned getbits(Bits *b, int n)          /* LSB first */
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) v |= (unsigned)getbit(b) << i;
    return v;
}

/* ------------------------------------------------------------ Shannon-Fano trees */
typedef struct { short count[17], symbol[256]; } Tree;

static int read_tree(Bits *b, Tree *t, int nsym)
{
    if (b->nb) return -1;                         /* trees start byte-aligned */
    if (b->p >= b->end) return -1;
    int nbytes = *b->p++ + 1, n = 0;
    unsigned char len[256];
    for (int i = 0; i < nbytes; i++) {
        if (b->p >= b->end) return -1;
        int v = *b->p++, l = (v & 15) + 1, c = (v >> 4) + 1;
        if (n + c > nsym) return -1;
        while (c--) len[n++] = (unsigned char)l;
    }
    if (n != nsym) return -1;
    memset(t->count, 0, sizeof t->count);
    for (int i = 0; i < nsym; i++) t->count[len[i]]++;
    short offs[18] = { 0 };
    for (int l = 1; l <= 16; l++) offs[l + 1] = (short)(offs[l] + t->count[l]);
    for (int i = 0; i < nsym; i++) t->symbol[offs[len[i]]++] = (short)i;
    return 0;
}

static int decode(Bits *b, const Tree *t)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 16; len++) {
        code |= getbit(b) ^ 1;                    /* PKWARE codes are the complement */
        int count = t->count[len];
        if (code - count < first) return t->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    b->err = 1;
    return 0;
}

static int explode(const uint8_t *in, size_t n, uint8_t *out, size_t usize, int flags)
{
    Bits b = { in, in + n, 0, 0, 0 };
    Tree lit, len, dist;
    int big = flags & 2, has_lit = flags & 4;
    if (has_lit && read_tree(&b, &lit, 256)) return -1;
    if (read_tree(&b, &len, 64) || read_tree(&b, &dist, 64)) return -1;
    int dbits = big ? 7 : 6, minlen = has_lit ? 3 : 2;
    size_t o = 0;
    while (o < usize && !b.err) {
        if (getbit(&b)) {
            out[o++] = (uint8_t)(has_lit ? decode(&b, &lit) : (int)getbits(&b, 8));
            continue;
        }
        size_t d = getbits(&b, dbits);
        d |= (size_t)decode(&b, &dist) << dbits;
        d += 1;
        size_t l = (size_t)decode(&b, &len);
        if (l == 63) l += getbits(&b, 8);
        l += (size_t)minlen;
        for (; l && o < usize; l--, o++) out[o] = o >= d ? out[o - d] : 0;   /* before start: zeros */
    }
    return b.err ? -1 : 0;
}

/* ------------------------------------------------------------ shrink (method 1) */
/* LZW, 9..13-bit codes; 256 is a control code: 256,1 = one more bit,
 * 256,2 = partial clear (free every entry that is nobody's prefix).
 * New entries take the lowest free code >= 257. */
#define SH_MAX 8192
#define SH_FREE (-1)
static int unshrink(const uint8_t *in, size_t n, uint8_t *out, size_t usize)
{
    static short parent[SH_MAX];
    static uint8_t value[SH_MAX], stack[SH_MAX];
    Bits b = { in, in + n, 0, 0, 0 };
    for (int i = 0; i < SH_MAX; i++) parent[i] = i < 256 ? (short)-2 : SH_FREE;   /* -2 = root */
    for (int i = 0; i < 256; i++) value[i] = (uint8_t)i;
    int bits = 9, prev = -1, next = 257;
    size_t o = 0;
    while (o < usize) {
        int code = (int)getbits(&b, bits);
        if (b.err) return -1;
        if (code == 256) {
            int c = (int)getbits(&b, bits);
            if (c == 1) { if (++bits > 13) return -1; }
            else if (c == 2) {
                static uint8_t used[SH_MAX];
                memset(used, 0, sizeof used);
                for (int i = 257; i < SH_MAX; i++) if (parent[i] >= 257) used[parent[i]] = 1;
                for (int i = 257; i < SH_MAX; i++) if (!used[i]) parent[i] = SH_FREE;
                next = 257;
                while (next < SH_MAX && parent[next] != SH_FREE) next++;
            } else return -1;
            continue;
        }
        int kwk = 0, cur = code;
        if (parent[code] == SH_FREE) {                /* KwKwK: the code being defined now */
            if (code != next || prev < 0) return -1;
            kwk = 1;
            cur = prev;
        }
        int sp = 0;
        for (int c = cur; c != -2 && sp < SH_MAX; c = parent[c]) {
            if (c == SH_FREE || parent[c] == SH_FREE) return -1;
            stack[sp++] = value[c];
            if (parent[c] == -2) break;
        }
        uint8_t head = stack[sp - 1];
        while (sp && o < usize) out[o++] = stack[--sp];
        if (kwk && o < usize) out[o++] = head;
        if (prev >= 0 && next < SH_MAX) {
            parent[next] = (short)prev;
            value[next] = head;
            while (next < SH_MAX && parent[next] != SH_FREE) next++;
        }
        prev = code;
    }
    return 0;
}

/* ------------------------------------------------------------ crc / container */
static uint32_t crc32_buf(const uint8_t *p, size_t n)
{
    static uint32_t tab[256];
    if (!tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static unsigned r16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p) | (uint32_t)r16(p + 2) << 16; }

int zip_extract_all(const char *zip, const char *dst_dir, char *err, size_t errsz)
{
    FILE *f = fopen(zip, "rb");
    if (!f) { snprintf(err, errsz, "cannot open %s", zip); return -1; }
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *z = fl > 0 ? malloc((size_t)fl) : NULL;
    if (!z || fread(z, 1, (size_t)fl, f) != (size_t)fl) { fclose(f); free(z); snprintf(err, errsz, "cannot read %s", zip); return -1; }
    fclose(f);
    size_t n = (size_t)fl, pos = 0;
    int files = 0, rc = 0;
    while (pos + 30 <= n && r32(z + pos) == 0x04034b50u) {
        const uint8_t *h = z + pos;
        unsigned flags = r16(h + 6), method = r16(h + 8), nlen = r16(h + 26), xlen = r16(h + 28);
        uint32_t crc = r32(h + 14), csize = r32(h + 18), usize = r32(h + 22);
        size_t data = pos + 30 + nlen + xlen;
        char name[256];
        if (nlen == 0 || nlen >= sizeof name || data + csize > n || (flags & 8)) { snprintf(err, errsz, "bad entry in %s", zip); rc = -1; break; }
        memcpy(name, h + 30, nlen);
        name[nlen] = 0;
        if (strchr(name, '/') || strchr(name, '\\') || strchr(name, ':') || !strcmp(name, "..")) {   /* flat archives only */
            snprintf(err, errsz, "unexpected path %s", name); rc = -1; break;
        }
        uint8_t *out = malloc(usize ? usize : 1);
        if (!out) { snprintf(err, errsz, "out of memory"); rc = -1; break; }
        int r;
        if (method == 0 && csize == usize) { memcpy(out, z + data, usize); r = 0; }
        else if (method == 6) r = explode(z + data, csize, out, usize, (int)flags);
        else if (method == 1) r = unshrink(z + data, csize, out, usize);
        else { snprintf(err, errsz, "%s: unsupported method %u", name, method); free(out); rc = -1; break; }
        if (r || crc32_buf(out, usize) != crc) { snprintf(err, errsz, "%s: corrupt data", name); free(out); rc = -1; break; }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dst_dir, name);
        FILE *o = fopen(path, "wb");
        int ok = o && fwrite(out, 1, usize, o) == usize;
        if (o && fclose(o)) ok = 0;
        free(out);
        if (!ok) { snprintf(err, errsz, "cannot write %s", path); rc = -1; break; }
        files++;
        pos = data + csize;
    }
    free(z);
    if (!rc && !files) { snprintf(err, errsz, "%s is not a zip archive", zip); rc = -1; }
    return rc;
}
