/* fr2_run.c - load and run the Fox Ranger II program chain (see fr2_modules.h). */
#include "fr2_modules.h"
#include "fr2_translation.h"
#include "cheat.h"
#include "cpu.h"
#include "crt_shims.h"
#include "ctvoice.h"
#include "audio.h"
#include "dos.h"
#include "heap.h"
#include "mem.h"
#include "mz_load.h"
#include "platform.h"
#include "recomp_rt.h"
#include "vga_modex.h"
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_st1_gen.h"
#include "recomp_st2_gen.h"
#include "recomp_st22_gen.h"
#include "recomp_st3_gen.h"
#include "recomp_st32_gen.h"
#include "recomp_st4_gen.h"
#include "recomp_st42_gen.h"
#include "recomp_st5_gen.h"
#include "recomp_st6_gen.h"
#include "recomp_st62_gen.h"
#include "recomp_st63_gen.h"
#include "recomp_fox2b_gen.h"
#include "recomp_tit_gen.h"
#include "recomp_start_gen.h"
#include "recomp_pwd_gen.h"
#include "recomp_score_gen.h"
#include "recomp_so_gen.h"
#include "recomp_ove_gen.h"
#include "recomp_pro1_gen.h"
#include "recomp_pro2_gen.h"
#include "recomp_pro3_gen.h"
#include "recomp_pro4_gen.h"
#include "recomp_vis1_gen.h"
#include "recomp_vis2_gen.h"
#include "recomp_vis4_gen.h"
#include "recomp_vis5_gen.h"
#include "recomp_epil_gen.h"

#define LOAD_SEG 0x0800

static int trace(void);

static const struct { const char *name; void (*reg)(void); } g_regs[] = {
    { "st1", recomp_register_st1 },     { "st2", recomp_register_st2 },
    { "st22", recomp_register_st22 },   { "st3", recomp_register_st3 },
    { "st32", recomp_register_st32 },   { "st4", recomp_register_st4 },
    { "st42", recomp_register_st42 },   { "st5", recomp_register_st5 },
    { "st6", recomp_register_st6 },     { "st62", recomp_register_st62 },
    { "st63", recomp_register_st63 },   { "fox2b", recomp_register_fox2b },
    { "tit", recomp_register_tit },     { "start", recomp_register_start },
    { "pwd", recomp_register_pwd },     { "score", recomp_register_score },
    { "so", recomp_register_so },       { "ove", recomp_register_ove },
    { "pro1", recomp_register_pro1 },   { "pro2", recomp_register_pro2 },
    { "pro3", recomp_register_pro3 },   { "pro4", recomp_register_pro4 },
    { "vis1", recomp_register_vis1 },   { "vis2", recomp_register_vis2 },
    { "vis4", recomp_register_vis4 },   { "vis5", recomp_register_vis5 },
    { "epil", recomp_register_epil },
};

const Fr2Module *fr2_module_find(const char *name)
{
    for (const Fr2Module *m = g_fr2_modules; m->name; m++)
        if (!strcasecmp(m->name, name)) return m;
    return NULL;
}

/* ------------------------------------------------------------ natives */

/* ST1 0891:0008 - builds the sine/cosine tables (x87 code through the MSC
 * emulator interrupts, which the recompiler does not translate):
 *   for (i = 0, d = 0.0; d < 360.0; d += 1.0, i++) {
 *       sin_tab[i] = (long)(sin(d * 3.141592 / 180.0) * 100);   es=[0E96]:3104
 *       cos_tab[i] = (long)(cos(d * 3.141592 / 180.0) * 100);   es=[0E98]:36A6
 *   }
 * The constants are the game's own (DGROUP 0E86 = 3.141592, 0E8E = 180.0). */
typedef struct { uint16_t k1, k2, sin_ptr, sin_off, cos_ptr, cos_off; } TrigLayout;
static void trig_tables(Cpu *c, const TrigLayout *L)
{
    double k1, k2;
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = rd8(c->ds, (uint16_t)(L->k1 + i));
    memcpy(&k1, b, 8);
    for (int i = 0; i < 8; i++) b[i] = rd8(c->ds, (uint16_t)(L->k2 + i));
    memcpy(&k2, b, 8);
    uint16_t s_seg = rd16(c->ds, L->sin_ptr), c_seg = rd16(c->ds, L->cos_ptr);
    int i = 0;
    for (double d = 0.0; d < 360.0; d += 1.0, i++) {
        double a = d * k1 / k2;
        int32_t sv = (int32_t)(sin(a) * 100.0), cv = (int32_t)(cos(a) * 100.0);
        wr16(s_seg, (uint16_t)(L->sin_off + 4 * i), (uint16_t)sv);
        wr16(s_seg, (uint16_t)(L->sin_off + 4 * i + 2), (uint16_t)((uint32_t)sv >> 16));
        wr16(c_seg, (uint16_t)(L->cos_off + 4 * i), (uint16_t)cv);
        wr16(c_seg, (uint16_t)(L->cos_off + 4 * i + 2), (uint16_t)((uint32_t)cv >> 16));
    }
    c->ax = (uint16_t)i;
    c->sp = (uint16_t)(c->sp + 4);                         /* retf */
}
/* ST1 0891:0008 and FOX2B 014F:00A6 (identical source, different link) */
static void st1_trig_tables(Cpu *c)
{ static const TrigLayout L = { 0x0E86, 0x0E8E, 0x0E96, 0x3104, 0x0E98, 0x36A6 }; trig_tables(c, &L); }
static void fox2b_trig_tables(Cpu *c)
{ static const TrigLayout L = { 0x11BE, 0x11C6, 0x11CE, 0x125A, 0x11D0, 0x17FC }; trig_tables(c, &L); }

/* Creative's voice drivers are linked into the programs as raw blobs
 * (paragraph aligned, `jmp near` + signature) and called at seg:0000 with the
 * register ABI.  They program the SB DSP/DMA directly, so they are replaced. */
static void register_voice_drivers(uint32_t image_size, int verbose)
{
    uint32_t base = LIN(LOAD_SEG, 0);
    for (uint32_t a = 0; a + 16 < image_size; a += 16) {
        if (ram[base + a] != 0xE9) continue;
        const char *sig = (const char *)&ram[base + a + 3];
        RecompFn fn = NULL; const char *nm = NULL;
        if (!memcmp(sig, "CT-VOICE", 8)) { fn = ctvoice_entry; nm = "ctvoice_entry"; }
        else if (!memcmp(sig, "CTVDSK", 6)) { fn = ctvdsk_entry; nm = "ctvdsk_entry"; }
        if (!fn) continue;
        far_register((uint16_t)(a >> 4), 0x0000, fn, nm);
        if (verbose) fprintf(stderr, "fr2:   %s at %04X:0000\n", nm, a >> 4);
    }
}

/* FOX2B 129B:2EDD - MSC GRAPHICS.LIB _setvideomode(short mode).  It
 * dispatches through a near jump table (DS:103A) to per-mode handlers the
 * recompiler never saw, so it is replaced.  The game calls it exactly once,
 * with _MRES256COLOR (13h), from 014F:0507, and ignores the result.
 * Returns the number of text rows (0 = failure), like the original. */
static void fox2b_setvideomode(Cpu *c)
{
    uint16_t mode = rd16(c->ss, (uint16_t)(c->sp + 4));
    if (mode == 0x13 || mode == 0xFFFF) {
        vga_set_mode13(&g_vga);
        c->ax = 25;
    } else {
        fprintf(stderr, "fox2b: _setvideomode(%04Xh) unsupported\n", mode);
        c->ax = 0;
    }
    c->sp = (uint16_t)(c->sp + 4);                         /* retf */
}

/* FOX2B 061A:00E4 - the FLI player's int86x-style helper:
 *     unsigned flags = jsys(int intno, REGS far *in, REGS far *out);
 * REGS = { ax, bx, cx, dx, si, di, ds, es }.  It stores `intno` into its own
 * `int` instruction (cs:011A) before executing it, which the recompiler can't
 * follow (it generated `int 0`), so every DOS call of the player - open,
 * read, close of the KW123/KKUNG2/KWANG1.POI animations played when a
 * missile is fired - was dropped and the screen stayed black. */
static void fox2b_jsys(Cpu *c)
{
    uint16_t sp = c->sp;                                  /* [sp] = return ip:cs */
    uint8_t  n    = (uint8_t)rd16(c->ss, (uint16_t)(sp + 4));
    uint16_t io   = rd16(c->ss, (uint16_t)(sp + 6)), is = rd16(c->ss, (uint16_t)(sp + 8));
    uint16_t oo   = rd16(c->ss, (uint16_t)(sp + 10)), os = rd16(c->ss, (uint16_t)(sp + 12));
    uint16_t sbx = c->bx, ssi = c->si, sdi = c->di, ses = c->es, sds = c->ds, sbp = c->bp;

    wr8(SEG(0x061A), 0x011A, n);                          /* keep the guest's view consistent */
    c->ax = rd16(is, io);              c->bx = rd16(is, (uint16_t)(io + 2));
    c->cx = rd16(is, (uint16_t)(io + 4)); c->dx = rd16(is, (uint16_t)(io + 6));
    c->si = rd16(is, (uint16_t)(io + 8)); c->di = rd16(is, (uint16_t)(io + 10));
    c->ds = rd16(is, (uint16_t)(io + 12)); c->es = rd16(is, (uint16_t)(io + 14));
    rt_interrupt(c, n);
    wr16(os, oo, c->ax);               wr16(os, (uint16_t)(oo + 2), c->bx);
    wr16(os, (uint16_t)(oo + 4), c->cx); wr16(os, (uint16_t)(oo + 6), c->dx);
    wr16(os, (uint16_t)(oo + 8), c->si); wr16(os, (uint16_t)(oo + 10), c->di);
    wr16(os, (uint16_t)(oo + 12), c->ds); wr16(os, (uint16_t)(oo + 14), c->es);
    Flags F; rt_flags_in(c, &F);
    c->ax = rt_flags_word(c, &F);                         /* pushf; pop ax */
    c->bx = sbx; c->si = ssi; c->di = sdi; c->es = ses; c->ds = sds; c->bp = sbp;
    c->sp = (uint16_t)(sp + 4);                           /* retf */
}

/* Frame pacing for ST2, ST22, ST3, ST32, ST4, ST42.
 *
 * Every stage installs the same C timer handler (INT 08h, chained at the
 * BIOS 18.2 Hz cadence) that clears a "frame due" byte in DGROUP.  ST1, ST5,
 * ST6, ST62 and ST63 start each main-loop pass with
 *     while (flag) ;  flag = 1;
 * so the game runs one frame per BIOS tick (~18 fps).  These six stages still
 * clear the flag but their main loop never waits on it: on 1993 hardware
 * the 3D rendering was slow enough to hide that, but recompiled code runs
 * at several hundred frames per second.  The wait is restored here, around
 * the first call of the main loop (filtered on the return address, since the
 * same function may also be called elsewhere). */
typedef struct {
    const char *prog;
    uint16_t seg, off;          /* first function called by the main loop */
    uint32_t ret_lin;           /* image-linear return address in the loop */
    uint16_t flag;              /* DGROUP byte cleared by the timer handler */
    RecompFn fn;                /* the recompiled function */
} FrameSync;
static const FrameSync g_frame_sync[] = {
    { "st2",  0x030F, 0x00E2, 0x00110, 0x82, f_st2_030F_00E2 },
    { "st22", 0x02F6, 0x00EA, 0x000FB, 0x82, f_st22_02F6_00EA },
    { "st3",  0x0835, 0x00E8, 0x00113, 0xAC, f_st3_0835_00E8 },
    { "st32", 0x031C, 0x00E8, 0x00110, 0x82, f_st32_031C_00E8 },
    { "st4",  0x02B1, 0x00E8, 0x0010A, 0x82, f_st4_02B1_00E8 },
    { "st42", 0x02C1, 0x00F0, 0x00144, 0x82, f_st42_02C1_00F0 },
};
static const FrameSync *g_fs;

static void frame_sync_wait(Cpu *c)
{
    uint16_t ret_ip = rd16(c->ss, c->sp), ret_cs = rd16(c->ss, (uint16_t)(c->sp + 2));
    uint32_t ret_lin = ((uint32_t)(uint16_t)(ret_cs - g_load_seg) << 4) + ret_ip;
    if (ret_lin == g_fs->ret_lin && c->ifl) {
        uint64_t t0 = platform_now_us();      /* never hang if no tick ever comes */
        while (rd8(c->ds, g_fs->flag) && platform_now_us() - t0 < 200000) rt_poll_slow(c, 0);
        wr8(c->ds, g_fs->flag, 1);
    }
    g_fs->fn(c);
}

/* Konami code (cheat.c): infinite shield.  Each stage keeps the player's
 * shield in a far data segment, reached through a segment pointer in DGROUP
 * (`mov es, [ptr]; ... es:[off]`):
 *  - ST1: byte, shield units left, 6 = full; at 0 a life is lost and it resets
 *  - ST2..ST63: word, 4 = full, -1 per hit, some collisions set it to 0
 *    directly (ST42 183F).  The main loop calls the explosion routine `boom`
 *    while it is 0 (<= 0 in ST5); `boom` drops the ship 5 px per two frames,
 *    counting in the byte 0Dh above the shield, and at 6 takes a life.
 *    Pinning that counter instead freezes the explosion on screen forever.
 * The poll pins the shield at `full` so the HUD never shows damage, and
 * `boom` is hooked: an instant kill can zero the shield between two polls,
 * so with the cheat on the hook refills it and skips the explosion.
 * (The bonus stage FOX2B has no shield or lives.) */
typedef struct {
    const char *prog; uint16_t seg_ptr, off; uint8_t full;
    uint16_t boom_seg, boom_off; RecompFn boom;   /* ST2+: explosion routine */
} ShieldVar;
static const ShieldVar g_shield_vars[] = {
    { "st1",  0x0DB8, 0x3086, 6, 0, 0, NULL },
    { "st2",  0x0C5E, 0x0D48, 4, 0x030F, 0x04FE, f_st2_030F_04FE },
    { "st22", 0x0C60, 0x0D46, 4, 0x02F6, 0x0539, f_st22_02F6_0539 },
    { "st3",  0x0D96, 0x0D4C, 4, 0x0835, 0x0668, f_st3_0835_0668 },
    { "st32", 0x0C5A, 0x0D48, 4, 0x031C, 0x067E, f_st32_031C_067E },
    { "st4",  0x0C46, 0x0D48, 4, 0x02B1, 0x065E, f_st4_02B1_065E },
    { "st42", 0x0C48, 0x0D48, 4, 0x02C1, 0x0654, f_st42_02C1_0654 },
    { "st5",  0x0B8C, 0x09CE, 4, 0x02D7, 0x0428, f_st5_02D7_0428 },
    { "st6",  0x0C06, 0x0D4E, 4, 0x02E4, 0x053C, f_st6_02E4_053C },
    { "st62", 0x0C30, 0x0D4E, 4, 0x02BB, 0x0537, f_st62_02BB_0537 },
    { "st63", 0x0C14, 0x0D50, 4, 0x02F9, 0x0560, f_st63_02F9_0560 },
};
static const ShieldVar *g_shield;
static uint16_t g_shield_dgroup;

static void fr2_infinite_shield(void)
{
    if (!g_shield) return;
    uint16_t seg = rd16(SEG(g_shield_dgroup), g_shield->seg_ptr);
    if (!seg) return;
    if (g_shield->boom) wr16(seg, g_shield->off, g_shield->full);   /* word: ST5 can go negative */
    else wr8(seg, g_shield->off, g_shield->full);
}

static void shield_boom(Cpu *c)
{
    if (!cheat_is_infinite_shield()) { g_shield->boom(c); return; }
    fr2_infinite_shield();
    c->sp = (uint16_t)(c->sp + 4);                        /* retf: no explosion */
}

static void register_natives(const char *name, uint32_t image_size)
{
    cheat_fr2 = 1;
    cheat_shield_fn = fr2_infinite_shield;
    g_shield = NULL;
    for (size_t i = 0; i < sizeof g_shield_vars / sizeof g_shield_vars[0]; i++)
        if (!strcmp(name, g_shield_vars[i].prog)) {
            const Fr2Module *m = fr2_module_find(name);
            if (m) { g_shield = &g_shield_vars[i]; g_shield_dgroup = m->dgroup; }
            if (m && g_shield->boom) far_register(g_shield->boom_seg, g_shield->boom_off, shield_boom, "shield_boom");
        }

    g_fs = NULL;
    for (size_t i = 0; i < sizeof g_frame_sync / sizeof g_frame_sync[0]; i++)
        if (!strcmp(name, g_frame_sync[i].prog)) {
            g_fs = &g_frame_sync[i];
            far_register(g_fs->seg, g_fs->off, frame_sync_wait, "frame_sync_wait");
        }
    if (!strcmp(name, "st1")) far_register(0x0891, 0x0008, st1_trig_tables, "st1_trig_tables");
    if (!strcmp(name, "fox2b")) {
        far_register(0x014F, 0x00A6, fox2b_trig_tables, "fox2b_trig_tables");
        far_register(0x129B, 0x2EDD, fox2b_setvideomode, "fox2b_setvideomode");
        far_register(0x061A, 0x00E4, fox2b_jsys, "fox2b_jsys");
    }
    register_voice_drivers(image_size, trace());
}

/* ------------------------------------------------------------ exec chain */

typedef struct { char path[128]; int argc; char argv[16][64]; } ExecReq;
static ExecReq g_req;
static char g_cur[16];                 /* program being run */
static int g_skip_pending;
int fr2_skip_intro;
static jmp_buf g_exec_jb;
static int g_trace = -1;

static int trace(void)
{
    if (g_trace < 0) g_trace = getenv("FR2_TRACE_EXEC") != NULL;
    return g_trace;
}

static void set_req(const char *path, int argc, char **argv)
{
    memset(&g_req, 0, sizeof g_req);
    snprintf(g_req.path, sizeof g_req.path, "%s", path);
    for (int i = 0; i < argc && i < 16 && argv[i]; i++, g_req.argc++)
        snprintf(g_req.argv[i], sizeof g_req.argv[i], "%s", argv[i]);
}

/* Esc/Space during PRO1-PRO4: the platform calls this from inside the
 * running program; the jump happens at the next poll (see intro_poll). */
int fr2_intro_skip_key(void)
{
    if (strncmp(g_cur, "pro", 3)) return 0;
    g_skip_pending = 1;
    return 1;
}

/* P pauses only inside a stage, so it still types in the high-score name entry */
int fr2_pause_key(void)
{
    if (strncmp(g_cur, "st", 2) && strcmp(g_cur, "fox2b")) return 0;
    platform_toggle_pause();
    return 1;
}

static void exec_hook(const char *path, int argc, char **argv);
/* polled from the platform loop: leave the intro the way PRO4 does,
 * through the hub with route N (title screen); "L" reloads FR.REF */
static void (*g_prev_poll)(Cpu *c, uint32_t at);
static void intro_poll(Cpu *c, uint32_t at)
{
    if (g_prev_poll) g_prev_poll(c, at);
    if (g_skip_pending) {
        g_skip_pending = 0;
        char *av[] = { "fr2.exe", "L", "v", "N", NULL };
        exec_hook("fr2.exe", 4, av);
    }
}

int fr2_stage_fade = 1;

static void exec_hook(const char *path, int argc, char **argv)
{
    /* A cleared stage execs the next program straight away (ST4 1625: no fade);
     * on 1993 hardware the disk load hid that, here it is a hard cut. */
    if (fr2_stage_fade && !strncmp(g_cur, "st", 2) && strcmp(g_cur, "start")) platform_fade_out(1000);
    set_req(path, argc, argv);
    longjmp(g_exec_jb, 1);
}

/* "C:\FR2\ST1.EXE" -> "st1" */
static void prog_name(const char *path, char *out, size_t n)
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    size_t k = 0;
    for (; b[k] && b[k] != '.' && k + 1 < n; k++) out[k] = (char)tolower((unsigned char)b[k]);
    out[k] = 0;
}

static int find_exe(const char *name, char *out, size_t n)
{
    char up[32]; size_t k = 0;
    for (; name[k] && k + 1 < sizeof up; k++) up[k] = (char)toupper((unsigned char)name[k]);
    up[k] = 0;
    /* unpacked programs: <game>/unpacked/ (made by the launcher's import, see
     * pklite.h), else the dev copies from tools/unpack_exe.py */
    for (int i = 0; i < 2; i++) {
        if (i == 0) snprintf(out, n, "%s/unpacked/%s.EXE", dos_game_dir(), up);
        else snprintf(out, n, "work/pristine/%s.EXE", up);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return 0; }
    }
    return -1;
}

/* ------------------------------------------------------------ translation */

/* Redirect the program's Korean text pointers to English strings placed in a
 * heap block (outside the program's image, BSS, far BSS and stack). Every
 * pointer must still hold its original value, otherwise it is left alone. */
static void apply_translation(const char *name)
{
    size_t need = 0; int n = 0;
    for (const Fr2Text *t = g_fr2_texts; t->module; t++)
        if (!strcmp(t->module, name)) { need += strlen(t->english) + 1; n++; }
    if (!n) return;
    uint16_t seg = heap_alloc_paras((uint32_t)need);
    if (!seg) { fprintf(stderr, "fr2: %s: no memory for the translation\n", name); return; }
    uint16_t o = 0; int done = 0;
    uint32_t base = LIN(LOAD_SEG, 0);
    for (const Fr2Text *t = g_fr2_texts; t->module; t++) {
        if (strcmp(t->module, name)) continue;
        uint32_t a = base + t->ptr_img;
        if (mem_rd16(a) != t->off || mem_rd16(a + 2) != SEG(t->seg)) {
            fprintf(stderr, "fr2: %s: text pointer at %05X is %04X:%04X, expected %04X:%04X - left untranslated\n",
                    name, t->ptr_img, mem_rd16(a + 2), mem_rd16(a), SEG(t->seg), t->off);
            continue;
        }
        mem_wr16(a, o); mem_wr16(a + 2, seg);
        for (const char *p = t->english; ; p++) { wr8(seg, o++, (uint8_t)*p); if (!*p) break; }
        done++;
    }
    if (trace()) fprintf(stderr, "fr2:   %d/%d strings translated\n", done, n);
}

/* ------------------------------------------------------------ FR2.EXE hub */

#define FR_REF_SIZE 0x26
static int fr_ref_io(uint8_t *st, int save)
{
    char host[1024];
    if (dos_resolve("fr.ref", host, sizeof host, !save)) return -1;
    FILE *f = fopen(host, save ? "wb" : "rb");
    if (!f) return -1;
    size_t r = save ? fwrite(st, 1, FR_REF_SIZE, f) : fread(st, 1, FR_REF_SIZE, f);
    fclose(f);
    return r == FR_REF_SIZE ? 0 : -1;
}

/* FR2.EXE main (img 0010).  Returns 1 with g_req set to the next program,
 * 0 when FR2 itself exits. */
static int hub(void)
{
    int argc = g_req.argc;
    const char *argv[17] = { 0 };
    for (int i = 0; i < argc; i++) argv[i] = g_req.argv[i];

    uint8_t st[FR_REF_SIZE] = { 0 };
    fr_ref_io(st, 0);                                       /* 05D1 */
    if (argc < 2) { fputs("Usage : FR2 <I | A | M | S>\n", stdout); return 0; }
    char v = argv[2] ? argv[2][0] : 0;                      /* argv[2]==NULL reads DS:0 = 0 */
    st[1] = (uint8_t)(v == 'v' ? 2 : v == 'm' ? 1 : 0);
    switch (toupper((unsigned char)argv[1][0])) {
    case 'I': st[0] = 1; break;
    case 'A': st[0] = 2; break;
    case 'M': st[0] = 3; break;
    case 'S': st[0] = 4; break;
    case 'L': fr_ref_io(st, 0); break;
    default: fputs("Usage : SP <I | A | M | S>\n", stdout); return 0;
    }
    char route, s;
    if (argc < 4) {
        vga_set_mode13(&g_vga);
        route = fr2_skip_intro ? 'N' : 'P';                 /* N = title screen */
        st[3] = 3;                                          /* lives */
        fr_ref_io(st, 1);                                   /* 0610 */
        s = 0;
    } else {
        route = argv[3][0];
        s = argv[4] ? argv[4][0] : 0;
    }
    if (route < 'A' || route > 'Z') return 0;
    const Fr2Route *r = &g_fr2_routes[route - 'A'];
    const char *const *l = (s == 's' && r->s_variant) ? r->s_variant : r->normal;
    if (!strcmp(l[0], "@13")) { vga_set_mode13(&g_vga); l++; }
    char *nargv[17]; int n = 0;
    for (int i = 1; l[i]; i++) {
        if (!strcmp(l[i], "$4")) { if (!argv[4]) break; nargv[n++] = (char *)argv[4]; }
        else nargv[n++] = (char *)l[i];
    }
    nargv[n] = NULL;
    char path[64]; snprintf(path, sizeof path, "%s", l[0]);
    set_req(path, n, nargv);
    return 1;
}

/* ------------------------------------------------------------ program start */

static int run_program(const char *name)
{
    const Fr2Module *m = fr2_module_find(name);
    void (*reg)(void) = NULL;
    for (size_t i = 0; i < sizeof g_regs / sizeof *g_regs; i++) if (!strcmp(g_regs[i].name, name)) reg = g_regs[i].reg;
    char path[256];
    if (!m || !reg || find_exe(name, path, sizeof path)) {
        fprintf(stderr, "fr2: no program '%s'\n", name);
        return 1;
    }

    char tail[128] = { 0 };
    size_t tl = 0;
    for (int i = 1; i < g_req.argc; i++) {
        size_t a = strlen(g_req.argv[i]);
        if (tl + 1 + a >= sizeof tail) break;
        tail[tl++] = ' '; memcpy(tail + tl, g_req.argv[i], a); tl += a;
    }

    /* fresh conventional memory like a new DOS process (IVT/BIOS area kept) */
    memset(&ram[LIN(LOAD_SEG - 0x10, 0)], 0, VGA_WIN_BASE - LIN(LOAD_SEG - 0x10, 0));
    MzInfo info;
    if (mz_load(path, LOAD_SEG, tail, &info)) return 1;
    heap_init(info.end_seg, (uint16_t)(VGA_WIN_BASE >> 4));
    apply_translation(name);

    snprintf(g_cur, sizeof g_cur, "%s", name);
    for (int ch = 0; ch < 9; ch++) audio_opl_write((uint16_t)(0xB0 + ch), 0);   /* key off (no hanging notes) */
    far_reset();
    platform_install();
    if (rt_poll_fn != intro_poll) { g_prev_poll = rt_poll_fn; rt_poll_fn = intro_poll; }
    rt_isr_reset();
    rt_shadow_reset();
    crt_rand_reset();
    ctvoice_reset();
    register_natives(name, info.image_size);
    reg();
    for (const Fr2CrtEntry *e = m->crt; e->name; e++) {
        RecompFn fn = crt_shim_lookup(e->name);
        if (!fn) { fprintf(stderr, "fr2: %s: no shim for %s\n", name, e->name); return 1; }
        far_register(m->crt_seg, e->off, fn, e->name);
    }
    crt_init();

    RecompFn main_fn = far_lookup(SEG(m->main_seg), m->main_off);
    if (!main_fn) { fprintf(stderr, "fr2: %s: main %04X:%04X not recompiled\n", name, m->main_seg, m->main_off); return 1; }

    /* crt0: SS = DS = DGROUP, SP += sp_add, BSS cleared */
    Cpu c = { 0 };
    c.ds = c.es = c.ss = SEG(m->dgroup);
    c.sp = (uint16_t)((m->hdr_sp + m->sp_add) & 0xFFFE);
    c.ifl = 1;
    for (uint32_t o = m->bss_lo; o < m->bss_hi; o++) wr8(c.ds, (uint16_t)o, 0);

    /* argv / envp in a heap block: far pointer array, then the strings */
    uint16_t as = heap_alloc_paras(1024);
    uint16_t so = 0x100;
    for (int i = 0; i <= g_req.argc; i++) {
        if (i == g_req.argc) { wr16(as, (uint16_t)(4 * i), 0); wr16(as, (uint16_t)(4 * i + 2), 0); break; }
        const char *a = i ? g_req.argv[i] : path;
        if (!i) { static char a0[64]; snprintf(a0, sizeof a0, "C:\\FR2\\%s.EXE", name);
                  for (char *p = a0; *p; p++) *p = (char)toupper((unsigned char)*p); a = a0; }
        wr16(as, (uint16_t)(4 * i), so); wr16(as, (uint16_t)(4 * i + 2), as);
        for (const char *p = a; *p; p++) wr8(as, so++, (uint8_t)*p);
        wr8(as, so++, 0);
    }
    uint16_t envp_off = 0x80;                                  /* empty environment */
    wr16(as, envp_off, 0); wr16(as, (uint16_t)(envp_off + 2), 0);

    if (trace()) fprintf(stderr, "fr2: run %s%s (main %04X:%04X, DGROUP %04X)\n", name, tail, m->main_seg, m->main_off, m->dgroup);

    push16(&c, as); push16(&c, envp_off);                     /* envp */
    push16(&c, as); push16(&c, 0);                             /* argv */
    push16(&c, (uint16_t)g_req.argc);                          /* argc */
    push16(&c, SEG(m->crt_seg)); push16(&c, 0x0000);           /* return into crt0 */
    c.cs = SEG(m->main_seg);
    main_fn(&c);
    return (int16_t)c.ax;                                      /* crt0: exit(main()) */
}

int fr2_run_program(const char *prog, int argc, char **argv)
{
    crt_exec_hook = exec_hook;
    char *av[17]; av[0] = (char *)prog;
    for (int i = 1; i < argc && i < 16; i++) av[i] = argv[i];
    set_req(prog, argc < 1 ? 1 : argc, av);

    setjmp(g_exec_jb);                                         /* every execl lands here */
    for (;;) {
        char name[32];
        prog_name(g_req.path, name, sizeof name);
        if (trace()) {
            fprintf(stderr, "fr2: exec %s", g_req.path);
            for (int i = 1; i < g_req.argc; i++) fprintf(stderr, " %s", g_req.argv[i]);
            fputc('\n', stderr);
        }
        if (!strcmp(name, "fr2")) {
            if (!hub()) return 0;
            continue;
        }
        return run_program(name);
    }
}

int fr2_run(int argc, char **argv)
{
    return fr2_run_program("fr2.exe", argc, argv);
}
