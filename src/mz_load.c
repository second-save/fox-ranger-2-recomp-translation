#include "mz_load.h"
#include "mem.h"
#include "heap.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global game state mirroring the 38-byte FR.REF file */
fr_ref_state_t g_fr_ref = {
    .sound_device  = 2,     /* AdLib default (2) */
    .voice_enabled = 2,     /* CT-VOICE enabled (2) */
    .reserved02    = 0,
    .lives         = 3,     /* 3 lives default */
    .stage_id      = 0,
    .sub_stage     = 2,
    .weapon_level  = 2,
    .sub_weapon    = 0,
    .speed         = 2,
    .inventory     = {0},
    .difficulty    = 1,
    .padding       = {0}
};

void fr_ref_init_default(void)
{
    g_fr_ref.sound_device  = 2;
    g_fr_ref.voice_enabled = 2;
    g_fr_ref.reserved02    = 0;
    g_fr_ref.lives         = 3;
    g_fr_ref.stage_id      = 0;
    g_fr_ref.sub_stage     = 2;
    g_fr_ref.weapon_level  = 2;
    g_fr_ref.sub_weapon    = 0;
    g_fr_ref.speed         = 2;
    memset(g_fr_ref.inventory, 0, sizeof(g_fr_ref.inventory));
    g_fr_ref.difficulty    = 1;
    memset(g_fr_ref.padding, 0, sizeof(g_fr_ref.padding));
}

int fr_ref_load(const char *path)
{
    const char *p = (path && *path) ? path : "FR.REF";
    FILE *f = fopen(p, "rb");
    if (!f) {
        /* try lowercase */
        f = fopen("fr.ref", "rb");
    }
    if (!f) return -1;
    size_t n = fread(&g_fr_ref, 1, sizeof(g_fr_ref), f);
    fclose(f);
    return (n == sizeof(g_fr_ref)) ? 0 : -1;
}

int fr_ref_save(const char *path)
{
    const char *p = (path && *path) ? path : "FR.REF";
    FILE *f = fopen(p, "wb");
    if (!f) {
        f = fopen("fr.ref", "wb");
    }
    if (!f) return -1;
    size_t n = fwrite(&g_fr_ref, 1, sizeof(g_fr_ref), f);
    fclose(f);
    return (n == sizeof(g_fr_ref)) ? 0 : -1;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int mz_load(const char *path, uint16_t load_seg, const char *cmdtail, MzInfo *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return -1; }
    fclose(f);
    if (n < 0x1C || d[0] != 'M' || d[1] != 'Z') { fprintf(stderr, "%s: not an MZ file\n", path); free(d); return -1; }

    uint16_t last = le16(d + 2), pages = le16(d + 4), nrel = le16(d + 6), hdrpar = le16(d + 8);
    uint16_t minalloc = le16(d + 0x0A), ss = le16(d + 0x0E), sp = le16(d + 0x10);
    uint16_t ip = le16(d + 0x14), cs = le16(d + 0x16), relofs = le16(d + 0x18);
    uint32_t size = pages * 512u - (last ? 512u - last : 0u);
    uint32_t hdr = hdrpar * 16u;
    if (size > (uint32_t)n || hdr > size) { fprintf(stderr, "%s: bad header\n", path); free(d); return -1; }
    uint32_t img = size - hdr;
    if (LIN(load_seg, 0) + img > VGA_WIN_BASE) { fprintf(stderr, "%s: image too large\n", path); free(d); return -1; }

    g_load_seg = load_seg;
    memcpy(&ram[LIN(load_seg, 0)], d + hdr, img);

    for (uint16_t i = 0; i < nrel; i++) {
        const uint8_t *r = d + relofs + 4u * i;
        uint32_t a = LIN((uint16_t)(le16(r + 2) + load_seg), le16(r));
        uint16_t v = (uint16_t)(ram[a] | (ram[a + 1] << 8));
        v = (uint16_t)(v + load_seg);
        ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8);
    }

    /* PSP */
    uint16_t psp = (uint16_t)(load_seg - 0x10);
    uint8_t *P = &ram[LIN(psp, 0)];
    memset(P, 0, 256);
    P[0] = 0xCD; P[1] = 0x20;                              /* INT 20h */
    P[2] = 0x00; P[3] = 0xA0;                              /* top of memory = A000 */
    size_t tl = cmdtail ? strlen(cmdtail) : 0;
    if (tl > 126) tl = 126;
    P[0x80] = (uint8_t)tl;
    if (tl) memcpy(P + 0x81, cmdtail, tl);
    P[0x81 + tl] = 0x0D;

    if (out) {
        out->psp_seg = psp; out->load_seg = load_seg;
        out->cs = (uint16_t)(cs + load_seg); out->ip = ip;
        out->ss = (uint16_t)(ss + load_seg); out->sp = sp;
        out->image_size = img; out->nrelocs = nrel;
        out->end_seg = (uint16_t)(load_seg + (img + 15) / 16 + minalloc);
    }
    free(d);
    return 0;
}

