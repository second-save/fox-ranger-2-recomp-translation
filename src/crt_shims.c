#include "crt_shims.h"
#include "mz_load.h"
#include "dos.h"
#include "heap.h"
#include "io.h"
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

void (*crt_vector_hook)(uint8_t vec, uint16_t seg, uint16_t off);
uint8_t crt_interrupts_enabled = 1;

#define RET(c)      ((c)->sp = (uint16_t)((c)->sp + 4))
#define RETN(c, n)  ((c)->sp = (uint16_t)((c)->sp + 4 + (n)))
#define SETDXAX(c, v) do { uint32_t _v = (uint32_t)(v); (c)->ax = (uint16_t)_v; (c)->dx = (uint16_t)(_v >> 16); } while (0)
#define ARG(n)      farg(c, (n))
#define ARGP(n)     farg_ptr(c, (n))
#define ARGL(n)     ((int32_t)(((uint32_t)farg(c, (n) + 1) << 16) | farg(c, (n))))

/* ---------------------------------------------------- emulated FILE table
 * MSC large-model struct _iobuf: char far *_ptr; int _cnt; char far *_base;
 * char _flag; char _file;  (12 bytes, padded to 16 here) */
#define IOB_N 20
#define _IOREAD 0x01
#define _IOWRT  0x02
#define _IOEOF  0x10
#define _IOERR  0x20
static uint16_t g_iob_seg;
static FILE    *g_iob_host[IOB_N];

void crt_init(void)
{
    for (int i = 0; i < IOB_N; i++) if (g_iob_host[i]) { fclose(g_iob_host[i]); g_iob_host[i] = NULL; }
    g_iob_seg = heap_alloc_paras(IOB_N * 16);
    for (int i = 0; i < IOB_N * 16; i++) wr8(g_iob_seg, (uint16_t)i, 0);
}

static int iob_index(FarPtr fp)
{
    if (fp.seg != g_iob_seg || fp.off % 16 || fp.off / 16 >= IOB_N) return -1;
    return g_iob_host[fp.off / 16] ? fp.off / 16 : -1;
}
static void iob_flag(int i, uint8_t set, uint8_t clr)
{
    uint16_t o = (uint16_t)(i * 16 + 10);
    wr8(g_iob_seg, o, (uint8_t)((rd8(g_iob_seg, o) & ~clr) | set));
}

static void s_fopen(Cpu *c)
{
    static int call_count = 0;
    char name[260], mode[16], host[1024];
    FarPtr n = ARGP(0), m = ARGP(2);
    dos_read_cstr(n.seg, n.off, name, sizeof name);
    dos_read_cstr(m.seg, m.off, mode, sizeof mode);
    (void)call_count++;
    int create = strchr(mode, 'w') || strchr(mode, 'a');
    if (!g_iob_seg) crt_init();
    int i = 0;
    while (i < IOB_N && g_iob_host[i]) i++;
    FILE *f = NULL;
    int res = dos_resolve(name, host, sizeof host, !create);
    if (i < IOB_N && !res) {
        char hm[8]; size_t k = 0;
        for (const char *p = mode; *p && k < 5; p++) if (strchr("rwa+", *p)) hm[k++] = *p;
        hm[k++] = 'b'; hm[k] = 0;                         /* always binary on the host */
        f = fopen(host, hm);
    }
    if (getenv("FR2_TRACE_IO")) fprintf(stderr, "fopen(\"%s\", \"%s\") -> %s\n", name, mode, f ? host : "FAILED");
    if (!f) {
        SETDXAX(c, 0); RET(c); return;
    }
    g_iob_host[i] = f;
    uint16_t o = (uint16_t)(i * 16);
    for (int k = 0; k < 16; k++) wr8(g_iob_seg, (uint16_t)(o + k), 0);
    wr8(g_iob_seg, (uint16_t)(o + 10), (uint8_t)((strchr(mode, 'r') ? _IOREAD : 0) | (create ? _IOWRT : 0) | (strchr(mode, '+') ? _IOREAD | _IOWRT : 0)));
    wr8(g_iob_seg, (uint16_t)(o + 11), (uint8_t)(5 + i));
    c->ax = o; c->dx = g_iob_seg;
    RET(c);
}

static void s_fclose(Cpu *c)
{
    int i = iob_index(ARGP(0));
    if (i < 0) { c->ax = 0xFFFF; RET(c); return; }
    fclose(g_iob_host[i]); g_iob_host[i] = NULL;
    for (int k = 0; k < 16; k++) wr8(g_iob_seg, (uint16_t)(i * 16 + k), 0);
    c->ax = 0; RET(c);
}

static void s_fread(Cpu *c)
{
    FarPtr buf = ARGP(0);
    uint16_t size = ARG(2), n = ARG(3);
    int i = iob_index(ARGP(4));
    if (getenv("FR2_TRACE_IO")) {
        uint16_t ro = rd16(c->ss, c->sp), rs = rd16(c->ss, (uint16_t)(c->sp + 2));
        fprintf(stderr, "fread(%04X:%04X, %u, %u, iob %d) from %04X:%04X\n", buf.seg, buf.off, size, n, i, (uint16_t)(rs - g_load_seg), ro);
    }
    if (i < 0 || !size) { c->ax = 0; RET(c); return; }
    uint32_t want = (uint32_t)size * n, got = 0;
    uint8_t tmp[4096];
    while (got < want) {
        size_t chunk = want - got > sizeof tmp ? sizeof tmp : want - got;
        size_t r = fread(tmp, 1, chunk, g_iob_host[i]);
        for (size_t k = 0; k < r; k++) wr8(buf.seg, (uint16_t)(buf.off + got + k), tmp[k]);
        got += (uint32_t)r;
        if (r < chunk) { iob_flag(i, feof(g_iob_host[i]) ? _IOEOF : _IOERR, 0); break; }
    }
    c->ax = (uint16_t)(got / size); RET(c);
}

static void s_fwrite(Cpu *c)
{
    FarPtr buf = ARGP(0);
    uint16_t size = ARG(2), n = ARG(3);
    int i = iob_index(ARGP(4));
    if (i < 0 || !size) { c->ax = 0; RET(c); return; }
    uint32_t want = (uint32_t)size * n, put = 0;
    for (; put < want; put++)
        if (fputc(rd8(buf.seg, (uint16_t)(buf.off + put)), g_iob_host[i]) == EOF) { iob_flag(i, _IOERR, 0); break; }
    c->ax = (uint16_t)(put / size); RET(c);
}

static void s_fseek(Cpu *c)
{
    int i = iob_index(ARGP(0));
    int32_t off = ARGL(2);
    uint16_t whence = ARG(4);
    if (i < 0 || whence > 2) { c->ax = 0xFFFF; RET(c); return; }
    int r = fseek(g_iob_host[i], off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    if (!r) iob_flag(i, 0, _IOEOF);
    c->ax = r ? 0xFFFF : 0; RET(c);
}

static void s_puts(Cpu *c)
{
    char s[512]; FarPtr p = ARGP(0);
    dos_read_cstr(p.seg, p.off, s, sizeof s);
    puts(s); fflush(stdout);
    c->ax = 0; RET(c);
}

static void s_exit(Cpu *c)
{
    int code = (int16_t)ARG(0);
    if (dos_exit_hook) dos_exit_hook(code);
    exit(code);
}

/* ---------------------------------------------------------------- heap */
static void s_fmalloc(Cpu *c)
{
    uint16_t seg = heap_alloc_paras(ARG(0) ? ARG(0) : 1);
    if (getenv("FR2_TRACE_HEAP")) fprintf(stderr, "_fmalloc(%u) -> %04X:0000\n", ARG(0), seg);
    c->ax = 0; c->dx = seg; RET(c);                      /* NULL = 0000:0000 on failure */
}
static void s_ffree(Cpu *c)  { FarPtr p = ARGP(0); if (p.seg && !p.off) heap_free(p.seg); RET(c); }
static void s_fmsize(Cpu *c) { FarPtr p = ARGP(0); c->ax = (uint16_t)heap_block_bytes(p.seg); RET(c); }

static void s_dos_allocmem(Cpu *c)
{
    uint16_t paras = ARG(0); FarPtr out = ARGP(1);
    uint16_t seg = heap_alloc_paras((uint32_t)paras * 16);

    if (seg) { wr16(out.seg, out.off, seg); c->ax = 0; }
    else { wr16(out.seg, out.off, heap_largest_free()); c->ax = 8; }   /* insufficient memory */
    RET(c);
}

/* -------------------------------------------------------------- strings */
static void s_fstrlen(Cpu *c)
{
    FarPtr p = ARGP(0); uint16_t n = 0;
    while (rd8(p.seg, (uint16_t)(p.off + n))) n++;
    c->ax = n; RET(c);
}

static void s_fstrcpy(Cpu *c)
{
    FarPtr d = ARGP(0), s = ARGP(2);
    uint16_t k = 0; uint8_t ch;
    do { ch = rd8(s.seg, (uint16_t)(s.off + k)); wr8(d.seg, (uint16_t)(d.off + k), ch); k++; } while (ch);
    c->ax = d.off; c->dx = d.seg; RET(c);
}

static void s_fmemcpy(Cpu *c)
{
    FarPtr d = ARGP(0), s = ARGP(2); uint16_t n = ARG(4);
    for (uint16_t k = 0; k < n; k++) wr8(d.seg, (uint16_t)(d.off + k), rd8(s.seg, (uint16_t)(s.off + k)));
    c->ax = d.off; c->dx = d.seg; RET(c);
}

static void s_fmemmove(Cpu *c)
{
    FarPtr d = ARGP(0), s = ARGP(2); uint16_t n = ARG(4);
    if (LIN(d.seg, d.off) > LIN(s.seg, s.off))           /* original compares linear addresses */
        for (uint16_t k = n; k-- > 0;) wr8(d.seg, (uint16_t)(d.off + k), rd8(s.seg, (uint16_t)(s.off + k)));
    else
        for (uint16_t k = 0; k < n; k++) wr8(d.seg, (uint16_t)(d.off + k), rd8(s.seg, (uint16_t)(s.off + k)));
    c->ax = d.off; c->dx = d.seg; RET(c);
}

static void s_fmemset(Cpu *c)
{
    FarPtr d = ARGP(0); uint8_t v = (uint8_t)ARG(2); uint16_t n = ARG(3);
    for (uint16_t k = 0; k < n; k++) wr8(d.seg, (uint16_t)(d.off + k), v);
    c->ax = d.off; c->dx = d.seg; RET(c);
}

/* -------------------------------------------------------------- sprintf
 * MSC printf subset as used by large-model code: flags -+ #0, width and
 * precision (incl. '*'), size prefixes h l F N, conversions d i u x X o c s %.
 * Varargs are read from the emulated stack (int = 16 bit, long = 32 bit,
 * %s = far pointer). */
int crt_sprintf(uint16_t bseg, uint16_t boff, uint16_t fseg, uint16_t foff, uint16_t ss, uint16_t argp)
{
    char fmt[512], out[2048];
    dos_read_cstr(fseg, foff, fmt, sizeof fmt);
    size_t o = 0;
#define POPW() (argp = (uint16_t)(argp + 2), rd16(ss, (uint16_t)(argp - 2)))
    for (const char *p = fmt; *p && o < sizeof out - 1; p++) {
        if (*p != '%') { out[o++] = *p; continue; }
        char spec[32]; size_t s = 0;
        spec[s++] = '%'; p++;
        while (*p && strchr("-+ #0", *p) && s < 8) spec[s++] = *p++;
        if (*p == '*') { s += (size_t)snprintf(spec + s, sizeof spec - s, "%d", (int16_t)POPW()); p++; }
        else while (*p >= '0' && *p <= '9' && s < 16) spec[s++] = *p++;
        if (*p == '.') {
            spec[s++] = *p++;
            if (*p == '*') { s += (size_t)snprintf(spec + s, sizeof spec - s, "%d", (int16_t)POPW()); p++; }
            else while (*p >= '0' && *p <= '9' && s < 24) spec[s++] = *p++;
        }
        int is_long = 0;
        while (*p && strchr("hlFNL", *p)) { if (*p == 'l') is_long = 1; p++; }
        char conv = *p;
        char piece[600]; int len = 0;
        switch (conv) {
        case 'd': case 'i': {
            long v;
            if (is_long) { uint16_t lo = POPW(); uint16_t hi = POPW(); v = (long)(int32_t)(((uint32_t)hi << 16) | lo); }
            else v = (long)(int16_t)POPW();
            spec[s++] = 'l'; spec[s++] = 'd'; spec[s] = 0;
            len = snprintf(piece, sizeof piece, spec, v); break; }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long v;
            if (is_long) { uint16_t lo = POPW(); uint16_t hi = POPW(); v = ((unsigned long)hi << 16) | lo; }
            else v = (unsigned long)POPW();
            spec[s++] = 'l'; spec[s++] = conv; spec[s] = 0;
            len = snprintf(piece, sizeof piece, spec, v); break; }
        case 'c':
            spec[s++] = 'c'; spec[s] = 0;
            len = snprintf(piece, sizeof piece, spec, (int)(uint8_t)POPW()); break;
        case 's': {
            uint16_t so = POPW(), sg = POPW();
            char str[512]; dos_read_cstr(sg, so, str, sizeof str);
            spec[s++] = 's'; spec[s] = 0;
            len = snprintf(piece, sizeof piece, spec, str); break; }
        case '%': piece[0] = '%'; len = 1; break;
        case 0: p--; break;
        default:
            fprintf(stderr, "sprintf: unsupported conversion %%%c\n", conv);
            break;
        }
        for (int k = 0; k < len && o < sizeof out - 1 && k < (int)sizeof piece; k++) out[o++] = piece[k];
    }
#undef POPW
    out[o] = 0;
    for (size_t k = 0; k <= o; k++) wr8(bseg, (uint16_t)(boff + k), (uint8_t)out[k]);
    return (int)o;
}

static void s_printf(Cpu *c)
{
    FarPtr f = ARGP(0);
    char buf[512];
    dos_read_cstr(f.seg, f.off, buf, sizeof buf);
    printf("CRT printf: %s\n", buf);
    c->ax = 0;
    RET(c);
}

/* execl(path, arg0, ..., NULL): replace the running program.  The module
 * runner owns the hook and never returns from it. */
void (*crt_exec_hook)(const char *path, int argc, char **argv);
static void s_execl(Cpu *c)
{
    FarPtr p_path = ARGP(0);
    char path[256] = {0};
    dos_read_cstr(p_path.seg, p_path.off, path, sizeof(path));
    char argv_storage[16][64];
    char *argv[17];
    int argc = 0;
    for (int i = 0; i < 16; i++) {
        uint16_t off = rd16(c->ss, (uint16_t)(c->sp + 4 + 4 + i * 4));
        uint16_t seg = rd16(c->ss, (uint16_t)(c->sp + 4 + 4 + i * 4 + 2));
        if (seg == 0 && off == 0) break;
        dos_read_cstr(seg, off, argv_storage[argc], sizeof(argv_storage[argc]));
        argv[argc] = argv_storage[argc];
        argc++;
    }
    argv[argc] = NULL;
    if (crt_exec_hook) crt_exec_hook(path, argc, argv);
    c->ax = 0xFFFF; RET(c);                               /* exec failed */
}

static void s_sprintf(Cpu *c)
{
    FarPtr b = ARGP(0), f = ARGP(2);
    c->ax = (uint16_t)crt_sprintf(b.seg, b.off, f.seg, f.off, c->ss, (uint16_t)(c->sp + 4 + 8));
    RET(c);
}

/* ---------------------------------------------------------------- misc */
static void s_inp(Cpu *c)  { c->ax = port_in8(ARG(0)); RET(c); }
static void s_outp(Cpu *c) { uint8_t v = (uint8_t)ARG(1); port_out8(ARG(0), v); c->ax = v; RET(c); }
static void s_abs(Cpu *c)  { int16_t v = (int16_t)ARG(0); c->ax = (uint16_t)(v < 0 ? -v : v); RET(c); }

/* MSC rand(): 32-bit state, x = x * 343FDh + 269EC3h, return (x >> 16) & 7FFFh.
 * The state lives in each program's DGROUP at a link-dependent address; only
 * rand/srand touch it, so it is kept host-side.  Initial seed 1 (MSC default). */
static uint32_t g_rand_state = 1;
void crt_rand_reset(void) { g_rand_state = 1; }
static void s_rand(Cpu *c)
{
    uint32_t x = g_rand_state * 0x343FDu + 0x269EC3u;
    g_rand_state = x;
    c->dx = (uint16_t)(x >> 16);
    c->ax = (uint16_t)((x >> 16) & 0x7FFF);
    RET(c);
}
static void s_srand(Cpu *c) { g_rand_state = ARG(0); RET(c); }

/* ------------------------------------------------- long arithmetic helpers */
int32_t crt_ldiv(int32_t a, int32_t b)
{
    if (!b) { fprintf(stderr, "_aFldiv: divide by zero\n"); return 0; }
    if (a == INT32_MIN && b == -1) return INT32_MIN;
    return a / b;                                       /* C99: truncation toward zero, like MSC */
}
int32_t crt_lrem(int32_t a, int32_t b)
{
    if (!b) { fprintf(stderr, "_aFlrem: divide by zero\n"); return 0; }
    if (a == INT32_MIN && b == -1) return 0;
    return a % b;                                       /* sign follows the dividend */
}
static void s_lmul(Cpu *c) { SETDXAX(c, (uint32_t)ARGL(0) * (uint32_t)ARGL(2)); RETN(c, 8); }
static void s_ldiv(Cpu *c) { SETDXAX(c, crt_ldiv(ARGL(0), ARGL(2))); RETN(c, 8); }
static void s_lrem(Cpu *c) { SETDXAX(c, crt_lrem(ARGL(0), ARGL(2))); RETN(c, 8); }
static void s_lshl(Cpu *c)
{
    uint32_t v = ((uint32_t)c->dx << 16) | c->ax;
    unsigned n = c->cl; c->ch = 0;
    while (n--) v <<= 1;                                /* loop semantics, count up to 255 */
    c->cx = 0; SETDXAX(c, v); RET(c);
}
static void s_lshr(Cpu *c)
{
    int32_t v = (int32_t)(((uint32_t)c->dx << 16) | c->ax);
    unsigned n = c->cl; c->ch = 0;
    while (n--) v = (v < 0) ? (int32_t)(((uint32_t)v >> 1) | 0x80000000u) : v >> 1;
    c->cx = 0; SETDXAX(c, (uint32_t)v); RET(c);
}

/* ------------------------------------------------------ interrupt plumbing */
static void s_disable(Cpu *c) { crt_interrupts_enabled = 0; c->ifl = 0; RET(c); }
static void s_enable(Cpu *c)  { crt_interrupts_enabled = 1; c->ifl = 1; RET(c); }

static void s_getvect(Cpu *c)
{
    uint8_t n = (uint8_t)ARG(0);
    c->ax = rd16(0, (uint16_t)(n * 4)); c->dx = rd16(0, (uint16_t)(n * 4 + 2));
    RET(c);
}
static void s_setvect(Cpu *c)
{
    uint8_t n = (uint8_t)ARG(0); FarPtr h = ARGP(1);
    if ((n == 8 || n == 9) && h.seg == 0 && h.off == 0) { RET(c); return; }
    wr16(0, (uint16_t)(n * 4), h.off); wr16(0, (uint16_t)(n * 4 + 2), h.seg);
    if (crt_vector_hook) crt_vector_hook(n, h.seg, h.off);
    RET(c);
}
/* _chain_intr(handler) (134D:2266): restores the registers saved by the
 * _interrupt prologue (frame at the ISR's BP: es ds di si bp sp bx dx cx ax,
 * then ip cs flags of the interrupt), jumps to `handler` with the interrupt
 * frame on the stack (its iret returns to the interrupted code) and never
 * returns: the ISR's C frame is abandoned via rt_isr_unwind(). */
#include "recomp_rt.h"
static void s_chain_intr(Cpu *c)
{
    FarPtr h = ARGP(0);
    uint16_t f = c->bp, ss = c->ss;
    c->es = rd16(ss, f);                 c->ds = rd16(ss, (uint16_t)(f + 2));
    c->di = rd16(ss, (uint16_t)(f + 4)); c->si = rd16(ss, (uint16_t)(f + 6));
    c->bx = rd16(ss, (uint16_t)(f + 0xC)); c->dx = rd16(ss, (uint16_t)(f + 0xE));
    c->cx = rd16(ss, (uint16_t)(f + 0x10)); c->ax = rd16(ss, (uint16_t)(f + 0x12));
    c->bp = rd16(ss, (uint16_t)(f + 8));
    c->sp = (uint16_t)(f + 0x14);         /* -> ip, cs, flags */
    RecompFn fn = far_lookup(h.seg, h.off);
    if (fn) {
        uint16_t saved = c->cs; c->cs = h.seg;
        rt_isr_call(c, fn);               /* handler irets: pops ip, cs, flags */
        c->cs = saved;
    } else {                              /* unknown target: BIOS INT 08h default, then iret */
        uint32_t t = ((uint32_t)rd16(0x40, 0x6E) << 16 | rd16(0x40, 0x6C)) + 1;
        wr16(0x40, 0x6C, (uint16_t)t); wr16(0x40, 0x6E, (uint16_t)(t >> 16));
        c->sp = (uint16_t)(c->sp + 4);
        uint16_t fl = pop16(c); c->ifl = (fl >> 9) & 1; c->df = (fl >> 10) & 1;
    }
    if (rt_isr_depth() > 0) rt_isr_unwind(c);
    RET(c);                               /* not inside a delivered interrupt (tests) */
}

/* _dos_findfirst(path, attr, struct find_t far*): attrib +15h, time +16h,
 * date +18h, size +1Ah (long), name +1Eh */
static void s_findfirst(Cpu *c)
{
    char path[260], host[1024];
    FarPtr p = ARGP(0), ft = ARGP(3);
    dos_read_cstr(p.seg, p.off, path, sizeof path);
    struct stat st;
    if (dos_resolve(path, host, sizeof host, 1) || stat(host, &st)) { c->ax = 2; RET(c); return; }
    for (int k = 0; k < 43; k++) wr8(ft.seg, (uint16_t)(ft.off + k), 0);
    wr8(ft.seg, (uint16_t)(ft.off + 0x15), 0x20);
    wr16(ft.seg, (uint16_t)(ft.off + 0x1A), (uint16_t)st.st_size);
    wr16(ft.seg, (uint16_t)(ft.off + 0x1C), (uint16_t)((uint32_t)st.st_size >> 16));
    const char *base = strrchr(host, '/'); base = base ? base + 1 : host;
    for (int k = 0; k < 12 && base[k]; k++) {
        char ch = base[k]; if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        wr8(ft.seg, (uint16_t)(ft.off + 0x1E + k), (uint8_t)ch);
    }
    c->ax = 0; RET(c);
}

void crt_register_segment(uint16_t seg)
{
    static const struct { uint16_t off; RecompFn fn; const char *name; } t[] = {
        { 0x01DF, s_exit, "exit" },          { 0x06A6, s_fclose, "fclose" },
        { 0x07A0, s_fopen, "fopen" },        { 0x07BC, s_fread, "fread" },
        { 0x092E, s_fwrite, "fwrite" },      { 0x0AC6, s_puts, "puts" },
        { 0x19CC, s_fmalloc, "_fmalloc" },   { 0x19D2, s_ffree, "_ffree" },
        { 0x1C92, s_fmsize, "_fmsize" },     { 0x1CE6, s_fstrcpy, "_fstrcpy" },
        { 0x1D1C, s_fstrlen, "_fstrlen" },   { 0x1D7C, s_fseek, "fseek" },
        { 0x1F98, s_sprintf, "sprintf" },    { 0x2004, s_inp, "inp" },
        { 0x2012, s_outp, "outp" },          { 0x2020, s_fmemmove, "_fmemmove" },
        { 0x2148, s_fmemset, "_fmemset" },   { 0x21F4, s_abs, "abs" },
        { 0x2214, s_rand, "rand" },          { 0x224C, s_dos_allocmem, "_dos_allocmem" },
        { 0x2266, s_chain_intr, "_chain_intr" }, { 0x2282, s_getvect, "_dos_getvect" },
        { 0x229F, s_findfirst, "_dos_findfirst" }, { 0x22D2, s_setvect, "_dos_setvect" },
        { 0x22E8, s_disable, "_disable" },   { 0x22EA, s_enable, "_enable" },
        { 0x22EC, s_ldiv, "_aFldiv" },       { 0x2386, s_lmul, "_aFlmul" },
        { 0x23B8, s_lrem, "_aFlrem" },       { 0x2458, s_lshl, "_aFlshl" },
        { 0x2464, s_lshr, "_aFlshr" },       { 0x2470, s_fmemcpy, "_fmemcpy" },
        { 0x24CE, s_fmemset, "_fmemset2" },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) far_register(seg, t[i].off, t[i].fn, t[i].name);
}

/* ------------------------------------------------ FR2-only run-time entries
 * (MSC 6 functions Fox Ranger 1 never linked; identified per module by
 * tools/crt_ident.py, see src/fr2_modules_gen.c) */

/* unsigned 32-bit remainder, callee pops 8 */
static void s_ulrem(Cpu *c)
{
    uint32_t a = (uint32_t)ARGL(0), b = (uint32_t)ARGL(2);
    SETDXAX(c, b ? a % b : 0); RETN(c, 8);
}

/* rewind(FILE far *) */
static void s_rewind(Cpu *c)
{
    int i = iob_index(ARGP(0));
    if (i >= 0) { fseek(g_iob_host[i], 0, SEEK_SET); iob_flag(i, 0, _IOEOF | _IOERR); }
    RET(c);
}

/* _dos_open(const char far *path, unsigned mode, int far *handle) -> 0 / DOS error */
static void s_dos_open(Cpu *c)
{
    char name[260]; FarPtr n = ARGP(0), out = ARGP(3);
    dos_read_cstr(n.seg, n.off, name, sizeof name);
    int h = dos_open(name, ARG(2) & 3, 0);
    if (h < 0) { c->ax = (uint16_t)-h; RET(c); return; }
    wr16(out.seg, out.off, (uint16_t)h); c->ax = 0; RET(c);
}
static void s_dos_close(Cpu *c) { int r = dos_close(ARG(0)); c->ax = (uint16_t)(r < 0 ? -r : 0); RET(c); }

/* open(const char far *path, int oflag [, int pmode]) -> handle / -1.
 * O_CREAT 0100h, O_TRUNC 0200h; access in the low two bits. */
static void s_open(Cpu *c)
{
    char name[260]; FarPtr n = ARGP(0); uint16_t of = ARG(2);
    dos_read_cstr(n.seg, n.off, name, sizeof name);
    int h = dos_open(name, of & 3, 0);
    if (h < 0 && (of & 0x0100)) h = dos_open(name, 2, 1);
    c->ax = (uint16_t)(h < 0 ? 0xFFFF : h); RET(c);
}
/* read(int fd, void far *buf, unsigned n) -> bytes / -1 (binary files only in FR2) */
static void s_read(Cpu *c)
{
    FILE *f = dos_handle_file(ARG(0)); FarPtr b = ARGP(1); uint16_t n = ARG(3), k = 0;
    if (!f) { c->ax = 0xFFFF; RET(c); return; }
    for (; k < n; k++) { int ch = fgetc(f); if (ch == EOF) break; wr8(b.seg, (uint16_t)(b.off + k), (uint8_t)ch); }
    c->ax = k; RET(c);
}

static void s_dos_freemem(Cpu *c) { c->ax = heap_free(ARG(0)) ? 9 : 0; RET(c); }

/* atol/atoi(const char far *) -> dx:ax */
static void s_atol(Cpu *c)
{
    FarPtr p = ARGP(0); char s[64];
    dos_read_cstr(p.seg, p.off, s, sizeof s);
    SETDXAX(c, (uint32_t)strtol(s, NULL, 10)); RET(c);
}

/* time(time_t far *t) -> seconds since 1970 (dx:ax) */
static void s_time(Cpu *c)
{
    FarPtr p = ARGP(0); uint32_t t = (uint32_t)time(NULL);
    if (p.seg || p.off) { wr16(p.seg, p.off, (uint16_t)t); wr16(p.seg, (uint16_t)(p.off + 2), (uint16_t)(t >> 16)); }
    SETDXAX(c, t); RET(c);
}

static void ltoa_into(Cpu *c, int32_t v, int is_long, FarPtr b, int radix)
{
    char tmp[40]; int n = 0; uint32_t u;
    int neg = (radix == 10 && v < 0);
    u = neg ? (uint32_t)(-(int64_t)v) : (is_long ? (uint32_t)v : (uint16_t)v);
    if (radix < 2 || radix > 36) radix = 10;
    do { int d = (int)(u % (uint32_t)radix); tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (uint32_t)radix; } while (u);
    uint16_t o = b.off;
    if (neg) wr8(b.seg, o++, '-');
    while (n) wr8(b.seg, o++, (uint8_t)tmp[--n]);
    wr8(b.seg, o, 0);
    c->ax = b.off; c->dx = b.seg;
}
static void s_itoa(Cpu *c) { ltoa_into(c, (int16_t)ARG(0), 0, ARGP(1), (int16_t)ARG(3)); RET(c); }
static void s_ltoa(Cpu *c) { ltoa_into(c, ARGL(0), 1, ARGP(2), (int16_t)ARG(4)); RET(c); }

/* getch(): blocks on the BIOS keyboard buffer */
int (*crt_getch_hook)(Cpu *c);
static void s_getch(Cpu *c) { c->ax = crt_getch_hook ? (uint16_t)(crt_getch_hook(c) & 0xFF) : 0x0D; RET(c); }

static void s_nop(Cpu *c) { RET(c); }

RecompFn crt_shim_lookup(const char *name)
{
    static const struct { const char *name; RecompFn fn; } t[] = {
        { "exit", s_exit }, { "fclose", s_fclose }, { "fopen", s_fopen }, { "fread", s_fread },
        { "fwrite", s_fwrite }, { "puts", s_puts }, { "_fmalloc", s_fmalloc }, { "_ffree", s_ffree },
        { "_fmsize", s_fmsize }, { "_fstrcpy", s_fstrcpy }, { "_fstrlen", s_fstrlen }, { "fseek", s_fseek },
        { "sprintf", s_sprintf }, { "inp", s_inp }, { "outp", s_outp }, { "_fmemmove", s_fmemmove },
        { "_fmemset", s_fmemset }, { "abs", s_abs }, { "rand", s_rand }, { "srand", s_srand },
        { "_dos_allocmem", s_dos_allocmem }, { "_dos_freemem", s_dos_freemem },
        { "_chain_intr", s_chain_intr }, { "_dos_getvect", s_getvect }, { "_dos_setvect", s_setvect },
        { "_dos_findfirst", s_findfirst }, { "_disable", s_disable }, { "_enable", s_enable },
        { "_aFldiv", s_ldiv }, { "_aFlmul", s_lmul }, { "_aFlrem", s_lrem }, { "_aFulrem", s_ulrem },
        { "_aFlshl", s_lshl }, { "_aFlshr", s_lshr }, { "_fmemcpy", s_fmemcpy }, { "printf", s_printf },
        { "execl", s_execl }, { "rewind", s_rewind }, { "_dos_open", s_dos_open }, { "_dos_close", s_dos_close },
        { "open", s_open }, { "read", s_read }, { "atol", s_atol }, { "time", s_time },
        { "itoa", s_itoa }, { "ltoa", s_ltoa }, { "getch", s_getch }, { "_nop_hook", s_nop },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(t[i].name, name)) return t[i].fn;
    return NULL;
}

void crt_register(void)
{
    crt_register_segment(CRT_SEG);
}
