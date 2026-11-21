#include "recomp_rt.h"
#include <stdio.h>
#include <stdlib.h>

int (*rt_call_hook)(Cpu *c, int is_far, uint16_t seg_img, uint16_t off);
void (*rt_call_log)(Cpu *c, uint16_t seg_img, uint16_t off, const char *name);
int32_t  rt_budget = INT32_MAX;
jmp_buf *rt_trap;
int32_t  rt_poll_countdown = 1024, rt_poll_interval = 1024;
void   (*rt_poll_fn)(Cpu *c, uint32_t at);

void rt_poll_slow(Cpu *c, uint32_t at)
{
    rt_poll_countdown = rt_poll_interval;
    if (rt_poll_fn) rt_poll_fn(c, at);
}

/* shadow call stack of far calls (recompiled C recursion mirrors the guest's) */
#define SHADOW 256
static struct { uint16_t seg, off, rcs, rip; } g_shadow[SHADOW];
static int g_shadow_n;
static void shadow_dump(void)
{
    fprintf(stderr, "far call stack (innermost last):\n");
    for (int i = g_shadow_n > SHADOW ? g_shadow_n - SHADOW : 0; i < g_shadow_n; i++)
        fprintf(stderr, "  %04X:%04X  called from %04X:%04X\n", g_shadow[i % SHADOW].seg, g_shadow[i % SHADOW].off,
                g_shadow[i % SHADOW].rcs, g_shadow[i % SHADOW].rip);
}
void rt_shadow_reset(void) { g_shadow_n = 0; }
void rt_shadow_print(void) { shadow_dump(); }
void rt_shadow_push(uint16_t seg, uint16_t off, uint16_t rcs, uint16_t rip)
{
    int sd = g_shadow_n++;
    g_shadow[sd % SHADOW].seg = seg; g_shadow[sd % SHADOW].off = off;
    g_shadow[sd % SHADOW].rcs = rcs; g_shadow[sd % SHADOW].rip = rip;
}
void rt_shadow_pop(void) { if (g_shadow_n) g_shadow_n--; }

#define ISR_MAX 16
static jmp_buf *g_isr_ctx[ISR_MAX];
static int g_isr_depth;

void rt_isr_call(Cpu *c, RecompFn fn)
{
    jmp_buf jb;
    if (g_isr_depth >= ISR_MAX) { fprintf(stderr, "interrupt nesting too deep\n"); abort(); }
    g_isr_ctx[g_isr_depth++] = &jb;
    int sd = g_shadow_n;
    if (!setjmp(jb)) fn(c);
    g_shadow_n = sd;
    g_isr_depth--;
}

int rt_isr_depth(void) { return g_isr_depth; }
void rt_isr_reset(void) { g_isr_depth = 0; }

void rt_isr_unwind(Cpu *c)
{
    (void)c;
    if (g_isr_depth > 0) longjmp(*g_isr_ctx[g_isr_depth - 1], 1);
}

static void recent_dump(void);
static void shadow_dump(void);

void rt_fault(Cpu *c, int code, uint32_t where, const char *what)
{
    (void)c;
    if (rt_trap) longjmp(*rt_trap, code);
    fprintf(stderr, "recomp fault %d (%s) at image %05X\n", code, what, where);
    recent_dump();
    shadow_dump();
    abort();
}

int rt_lenient_mode = 0;  /* If set, missing calls return instead of aborting */

/* last far calls, printed when something goes wrong */
#define RECENT 48
static struct { uint16_t seg, off, rcs, rip; const char *name; uint32_t rep; } g_recent[RECENT];
static unsigned g_recent_i;
static void recent_dump(void)
{
    fprintf(stderr, "recent far calls (oldest first):\n");
    for (unsigned k = 0; k < RECENT; k++) {
        unsigned i = (g_recent_i + k) % RECENT;
        if (!g_recent[i].name && !g_recent[i].seg && !g_recent[i].off) continue;
        fprintf(stderr, "  %04X:%04X %-28s from %04X:%04X", g_recent[i].seg, g_recent[i].off,
                g_recent[i].name ? g_recent[i].name : "?", g_recent[i].rcs, g_recent[i].rip);
        if (g_recent[i].rep) fprintf(stderr, "  (x%u)", g_recent[i].rep + 1);
        fputc('\n', stderr);
    }
}



static void missing(Cpu *c, int is_far, uint16_t seg, uint16_t off)
{
    uint16_t rip = rd16(c->ss, c->sp), rcs = is_far ? rd16(c->ss, (uint16_t)(c->sp + 2)) : c->cs;
    fprintf(stderr, "call to %04X:%04X: no recompiled or native function (from %04X:%04X, ds=%04X)\n",
            seg, off, (uint16_t)(rcs - g_load_seg), rip, (uint16_t)(c->ds - g_load_seg));
    recent_dump();
    shadow_dump();
    if (rt_trap) longjmp(*rt_trap, RT_TRAP_MISSING);
    if (rt_lenient_mode) {
        /* In lenient mode, skip the call: pop the return address and continue */
        c->sp = (uint16_t)(c->sp + (is_far ? 4 : 2));
        return;
    }
    abort();
}

void rt_call_near(Cpu *c, RecompFn fn, uint16_t seg_img, uint16_t off, uint16_t ret_ip)
{
    push16(c, ret_ip);
    if (rt_call_hook && rt_call_hook(c, 0, seg_img, off)) return;
    if (!fn) fn = far_lookup(SEG(seg_img), off);
    if (!fn) { missing(c, 0, seg_img, off); return; }
    fn(c);
}

void rt_call_far(Cpu *c, RecompFn fn, uint16_t seg_img, uint16_t off, uint16_t ret_cs, uint16_t ret_ip)
{
    push16(c, ret_cs);
    push16(c, ret_ip);
    if (rt_call_hook && rt_call_hook(c, 1, seg_img, off)) return;
    int flags = 0;
    const char *name = NULL;
    RecompFn found = far_lookup_ex(SEG(seg_img), off, &flags, &name);
    if (found) fn = found;
    if (rt_call_log) rt_call_log(c, seg_img, off, name);
    {
        uint16_t rcs = ret_cs ? (uint16_t)(ret_cs - g_load_seg) : 0;
        unsigned p = (g_recent_i + RECENT - 1) % RECENT;
        if (g_recent_i && g_recent[p].seg == seg_img && g_recent[p].off == off && g_recent[p].rip == ret_ip && g_recent[p].rcs == rcs) {
            g_recent[p].rep++;
        } else {
            unsigned i = g_recent_i++ % RECENT;
            g_recent[i].seg = seg_img; g_recent[i].off = off; g_recent[i].name = name;
            g_recent[i].rcs = rcs; g_recent[i].rip = ret_ip; g_recent[i].rep = 0;
        }
    }
    if (!fn) { missing(c, 1, seg_img, off); return; }
    uint16_t saved = c->cs;
    c->cs = SEG(seg_img);
    int sd = g_shadow_n++;
    if (sd >= 0) { g_shadow[sd % SHADOW].seg = seg_img; g_shadow[sd % SHADOW].off = off;
                   g_shadow[sd % SHADOW].rcs = (uint16_t)(ret_cs - g_load_seg); g_shadow[sd % SHADOW].rip = ret_ip; }
    if (flags & FAR_FN_ISR) rt_isr_call(c, fn);     /* `pushf; lcall [old]` chaining */
    else fn(c);
    g_shadow_n = sd;
    c->cs = saved;
}

void rt_call_far_ptr(Cpu *c, uint16_t seg_loaded, uint16_t off, uint16_t ret_cs, uint16_t ret_ip)
{
    uint16_t seg_img = (uint16_t)(seg_loaded - g_load_seg);
    rt_call_far(c, NULL, seg_img, off, ret_cs, ret_ip);
}
