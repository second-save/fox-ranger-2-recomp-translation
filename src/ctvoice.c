#include "ctvoice.h"
#include "audio.h"
#include "mem.h"
#include "platform.h"
#include "dos.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static struct {
    uint16_t base, irq;
    FarPtr   status;          /* status word address (fn 5) */
    int      have_status, speaker, playing;
} g_ct;

static void set_status(uint16_t v)
{
    if (g_ct.have_status) wr16(g_ct.status.seg, g_ct.status.off, v);
}

void ctvoice_pump(void)
{
    if (g_ct.playing && !audio_voice_busy()) { g_ct.playing = 0; set_status(0); }
}

uint32_t voc_decode(uint16_t seg, uint16_t off, uint8_t **pcm, uint32_t *rate)
{
    uint32_t a = LIN(seg, off), n = 0, cap = 0, r = 0;
    uint8_t *out = NULL;
    uint32_t rep_start = 0, rep_left = 0;
    if (a >= MEM_SIZE) {
        fprintf(stderr, "voc_decode: invalid addr %04X:%04X (lin %X >= MEM_SIZE)\n", seg, off, a);
        *pcm = NULL; *rate = 8000; return 0;
    }
    for (int guard = 0; guard < 4096; guard++) {
        if (a + 4 >= MEM_SIZE) break;
        uint8_t type = mem_rd8(a);
        if (type == 0) break;
        uint32_t len = mem_rd8(a + 1) | (mem_rd8(a + 2) << 8) | ((uint32_t)mem_rd8(a + 3) << 16);
        if (len > 0x100000 || a + 4 + len > MEM_SIZE) break;
        uint32_t body = a + 4, data = 0, dlen = 0, silence = 0;
        switch (type) {
        case 1:                                   /* sound data: rate byte, codec, samples */
            if (!r) r = 1000000u / (256u - mem_rd8(body));
            if (mem_rd8(body + 1) == 0) { data = body + 2; dlen = len >= 2 ? len - 2 : 0; }
            break;
        case 2: data = body; dlen = len; break;   /* continuation */
        case 3:                                   /* silence: u16 length-1, rate byte */
            silence = (uint32_t)(mem_rd8(body) | (mem_rd8(body + 1) << 8)) + 1;
            if (!r) r = 1000000u / (256u - mem_rd8(body + 2));
            break;
        case 6: rep_start = a + 4 + len; rep_left = (uint32_t)(mem_rd8(body) | (mem_rd8(body + 1) << 8)); break;
        case 7:
            if (rep_left && rep_left != 0xFFFF) { rep_left--; a = rep_start; continue; }
            break;                                /* infinite loops (FFFFh) play once */
        default: break;                           /* 4 marker, 5 text, 8/9 extended: skipped */
        }
        uint32_t add = dlen + silence;
        if (n + add > cap) {
            cap = (n + add) * 2 + 1024;
            uint8_t *nb = realloc(out, cap);
            if (!nb) { free(out); *pcm = NULL; return 0; }
            out = nb;
        }
        for (uint32_t k = 0; k < dlen; k++) out[n++] = mem_rd8(data + k);
        for (uint32_t k = 0; k < silence; k++) out[n++] = 0x80;
        a = body + len;
    }
    *pcm = out; *rate = r ? r : 8000;
    return n;
}

void ctvoice_entry(Cpu *c)
{
    ctvoice_pump();
    if (getenv("FR2_TRACE_VOICE")) fprintf(stderr, "ctvoice fn %u ax=%04X es:di=%04X:%04X\n", c->bx, c->ax, c->es, c->di);
    switch (c->bx) {
    case 0: c->ax = 0x0202; break;                          /* driver version 2.02 */
    case 1: g_ct.base = c->ax; c->ax = 0; break;
    case 2: g_ct.irq = c->ax; c->ax = 0; break;
    case 3: c->ax = 0; break;                               /* init ok */
    case 4: g_ct.speaker = c->al != 0; break;
    case 5: g_ct.status.seg = c->es; g_ct.status.off = c->di; g_ct.have_status = 1; set_status(0); break;
    case 6: {
        uint8_t *pcm; uint32_t rate;
        uint32_t n = voc_decode(c->es, c->di, &pcm, &rate);
        if (n) {
            audio_voice_play(pcm, n, rate);
            g_ct.playing = 1;
            set_status(0xFFFF);
        }
        free(pcm);
        c->ax = 0;
        break; }
    case 7: c->ax = 1; break;                               /* recording unsupported */
    case 8: case 12: audio_voice_stop(); g_ct.playing = 0; set_status(0); break;
    case 9: audio_voice_stop(); g_ct.playing = 0; set_status(0); break;
    case 10: case 11: c->ax = 0; break;                     /* pause/continue: accepted */
    default: fprintf(stderr, "ctvoice: unknown function %u\n", c->bx); c->ax = 1; break;
    }
    c->cf = 0;
    c->sp = (uint16_t)(c->sp + 4);                          /* retf */
}

/* ---- CTVDSK.DRV (disk double-buffer driver, FR2 PRO1-4 and PWD) ----------
 * Same register ABI as CT-VOICE except: fn 5 status word at DX:AX, fn 6 plays
 * the VOC stream read from the DOS file handle in AX (from its current
 * position), fn 14 returns the last error in DX:AX, fn 15 sets the disk
 * buffer (DX:AX, CX pages) and must precede fn 6 (else error 8). */
static uint16_t g_dsk_err, g_dsk_buf;

static uint32_t voc_decode_host(const uint8_t *b, size_t len, uint8_t **pcm, uint32_t *rate)
{
    size_t a = 0;
    if (len >= 0x1A && !memcmp(b, "Creative Voice File\x1a", 20)) a = (size_t)(b[0x14] | (b[0x15] << 8));
    uint32_t n = 0, cap = 0, r = 0;
    uint8_t *out = NULL;
    size_t rep_start = 0; uint32_t rep_left = 0;
    for (int guard = 0; guard < 65536 && a + 4 <= len; guard++) {
        uint8_t type = b[a];
        if (type == 0) break;
        uint32_t blen = (uint32_t)(b[a + 1] | (b[a + 2] << 8) | (b[a + 3] << 16));
        size_t body = a + 4;
        if (body + blen > len) blen = (uint32_t)(len - body);
        size_t data = 0; uint32_t dlen = 0, silence = 0;
        switch (type) {
        case 1: if (!r) r = 1000000u / (256u - b[body]);
                if (b[body + 1] == 0) { data = body + 2; dlen = blen >= 2 ? blen - 2 : 0; } break;
        case 2: data = body; dlen = blen; break;
        case 3: silence = (uint32_t)(b[body] | (b[body + 1] << 8)) + 1;
                if (!r) r = 1000000u / (256u - b[body + 2]); break;
        case 6: rep_start = body + blen; rep_left = (uint32_t)(b[body] | (b[body + 1] << 8)); break;
        case 7: if (rep_left && rep_left != 0xFFFF) { rep_left--; a = rep_start; continue; } break;
        default: break;
        }
        if (n + dlen + silence > cap) {
            cap = (n + dlen + silence) * 2 + 4096;
            uint8_t *nb = realloc(out, cap);
            if (!nb) { free(out); *pcm = NULL; return 0; }
            out = nb;
        }
        if (dlen) { memcpy(out + n, b + data, dlen); n += dlen; }
        if (silence) { memset(out + n, 0x80, silence); n += silence; }
        a = body + blen;
    }
    *pcm = out; *rate = r ? r : 8000;
    return n;
}

void ctvdsk_entry(Cpu *c)
{
    ctvoice_pump();
    if (getenv("FR2_TRACE_VOICE")) fprintf(stderr, "ctvdsk fn %u ax=%04X dx=%04X cx=%04X\n", c->bx, c->ax, c->dx, c->cx);
    uint16_t ret = 0;
    switch (c->bx) {
    case 0: ret = 0x0202; break;
    case 1: g_ct.base = c->ax; break;
    case 2: g_ct.irq = c->ax; break;
    case 3: g_dsk_err = 0; g_dsk_buf = c->ax ? c->ax : 1; break;   /* AX = buffer pages; driver allocates */
    case 4: g_ct.speaker = c->al != 0; break;
    case 5: g_ct.status.seg = c->dx; g_ct.status.off = c->ax; g_ct.have_status = 1; set_status(0); break;
    case 6: {
        if (g_ct.playing) { g_dsk_err = 2; ret = 1; break; }
        if (!g_dsk_buf) { g_dsk_err = 8; ret = 1; break; }
        FILE *f = dos_handle_file(c->ax);
        if (!f) { g_dsk_err = 6; ret = 1; break; }
        size_t cap = 1 << 16, len = 0, r;
        uint8_t *buf = malloc(cap);
        while (buf && (r = fread(buf + len, 1, cap - len, f)) > 0) {
            len += r;
            if (len == cap) { uint8_t *nb = realloc(buf, cap *= 2); if (!nb) break; buf = nb; }
        }
        uint8_t *pcm = NULL; uint32_t rate = 8000;
        uint32_t n = buf ? voc_decode_host(buf, len, &pcm, &rate) : 0;
        free(buf);
        if (n) { audio_voice_play(pcm, n, rate); g_ct.playing = 1; set_status(0xFFFF); }
        free(pcm);
        break; }
    case 7: g_dsk_err = 1; ret = 1; break;                  /* recording unsupported */
    case 8: case 9: case 12: audio_voice_stop(); g_ct.playing = 0; set_status(0); break;
    case 10: case 11: break;
    case 13: break;
    case 14: c->dx = 0; ret = g_dsk_err; break;
    case 15: g_dsk_buf = c->dx | c->ax; break;
    default: fprintf(stderr, "ctvdsk: unknown function %u\n", c->bx); ret = 1; break;
    }
    c->ax = ret;
    c->cf = 0;
    c->sp = (uint16_t)(c->sp + 4);                          /* retf */
}

void ctvoice_reset(void)
{
    audio_voice_stop();
    memset(&g_ct, 0, sizeof g_ct);
    g_dsk_err = 0; g_dsk_buf = 0;
}

/* ---- 159E/154A hardware probes: report SB at 220h, IRQ 7, plus AdLib ---- */
#define DSEG SEG(0x1ADF)

/* 159E:01F4 / 154A:0114 detect -> bitmask 1 = CMS, 2 = AdLib, 4 = SB DSP */
static void sb_detect(Cpu *c) { c->ax = 4 | 2; c->sp = (uint16_t)(c->sp + 4); }

/* 159E:00E8 / 154A:0008 IRQ probe -> [DS:10F2] = irq, [DS:10EC] = irq, AX = irq */
static void sb_irq_probe(Cpu *c)
{
    wr16(DSEG, 0x10F2, 7); wr8(DSEG, 0x10EC, 7);
    wr16(SEG(0x2020), 0x3E8, 7);
    wr16(SEG(0x2020), 0x3E6, 0x220);
    c->ax = 7; c->sp = (uint16_t)(c->sp + 4);
}

/* 159E:036C / 154A:0488 DSP reset -> AX = 0, CF = 0 */
static void sb_dsp_reset(Cpu *c) { c->ax = 0; c->cf = 0; c->sp = (uint16_t)(c->sp + 4); }
static void sb_irq_probe_fox2b(Cpu *c) { wr16(c->ds, 0x610, 7); c->ax = 7; c->sp = (uint16_t)(c->sp + 4); }

/* 1643:23B4 MPU-401 driver entry -> AX = 0, DX = 0 */
static void mpu401_entry(Cpu *c) { c->ax = 0; c->dx = 0; c->sp = (uint16_t)(c->sp + 4); }

void ctvoice_register(void)
{
    /* FR1 */
    far_register(CTVOICE_SEG, 0x0000, ctvoice_entry, "ctvoice_entry");
    far_register(SBGLUE_SEG, 0x01F4, sb_detect, "sb_detect");
    far_register(SBGLUE_SEG, 0x00E8, sb_irq_probe, "sb_irq_probe");
    far_register(SBGLUE_SEG, 0x036C, sb_dsp_reset, "sb_dsp_reset");

    /* FR2 */
    far_register(0x15A7, 0x0000, ctvoice_entry, "ctvoice_entry_fr2");
    far_register(0x154A, 0x0114, sb_detect, "sb_detect_fr2");
    far_register(0x154A, 0x0008, sb_irq_probe, "sb_irq_probe_fr2");
    far_register(0x154A, 0x0488, sb_dsp_reset, "sb_dsp_reset_fr2");
    far_register(0x0B3A, 0x00EC, sb_irq_probe_fox2b, "sb_irq_probe_fox2b");
    far_register(0x0B3A, 0x01F8, sb_detect, "sb_detect_fox2b");
    far_register(0x0B3A, 0x0370, sb_dsp_reset, "sb_dsp_reset_fox2b");
    far_register(0x0B86, 0x0000, ctvoice_entry, "ctvoice_entry_fox2b");
    far_register(0x1643, 0x23B4, mpu401_entry, "mpu401_entry");
}
