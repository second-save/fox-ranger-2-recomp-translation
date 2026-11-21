#include "io.h"
#include "vga_modex.h"
#include "audio.h"
#include <stdio.h>

IoState g_io;

/* Minimal Sound Blaster DSP (base 220h) so the games' own detection code
 * finds a card: reset (226h 1->0) queues AAh, 22Eh bit 7 = data available,
 * 22Ah pops the queue, 22Ch bit 7 clear = ready for a command.  Commands
 * E1h (version -> 3.02, SB Pro) and E0h (invert test) answer; F2h raises
 * the DSP IRQ (see io_sb_irq_pending).  Sample playback itself goes through
 * the native CT-VOICE/CTVDSK replacements (ctvoice.c), not DMA. */
static struct { uint8_t q[8]; int r, w; uint8_t reset, cmd, need; int irq; } g_dsp;
static void dsp_put(uint8_t v) { int n = (g_dsp.w + 1) % 8; if (n != g_dsp.r) { g_dsp.q[g_dsp.w] = v; g_dsp.w = n; } }
static void dsp_write(uint8_t v)
{
    if (g_dsp.need) {                         /* argument byte */
        g_dsp.need--;
        if (g_dsp.cmd == 0xE0) dsp_put((uint8_t)~v);
        return;
    }
    g_dsp.cmd = v;
    switch (v) {
    case 0xE1: dsp_put(3); dsp_put(2); break;
    case 0xE0: g_dsp.need = 1; break;
    case 0x40: g_dsp.need = 1; break;         /* time constant */
    case 0x14: case 0x24: g_dsp.need = 2; g_dsp.irq = 1; break;  /* DMA transfer: completes at once */
    case 0x48: g_dsp.need = 2; break;
    case 0xF2: g_dsp.irq = 1; break;
    case 0xD8: dsp_put(0xFF); break;           /* speaker status */
    default: break;
    }
}
int io_sb_irq_pending(void) { int p = g_dsp.irq; return p; }
void io_sb_irq_ack(void) { g_dsp.irq = 0; }

static int is_vga(uint16_t p) { return p >= 0x3B0 && p <= 0x3DF; }

double pit_ch0_hz(void)
{
    uint32_t d = g_io.pit_divisor[0] ? g_io.pit_divisor[0] : 65536u;
    return 1193181.667 / d;
}

void port_out8(uint16_t port, uint8_t v)
{
    if (g_io.trace) g_io.trace(1, port, v);
    if (is_vga(port)) { vga_port_out8(&g_vga, port, v); return; }
    switch (port) {
    case 0x20: return;                                   /* EOI */
    case 0x61: g_io.port61 = v; return;                  /* speaker gate / kbd ack */
    case 0x21: g_io.pic_imr = v; return;
    case 0x43: {                                         /* PIT control word */
        int ch = v >> 6;
        if (ch < 3) { g_io.pit_mode[ch] = v; g_io.pit_lohi[ch] = 0; }
        return; }
    case 0x40: case 0x41: case 0x42: {
        int ch = port - 0x40;
        if (g_io.pit_lohi[ch] == 0) { g_io.pit_divisor[ch] = (uint16_t)((g_io.pit_divisor[ch] & 0xFF00) | v); g_io.pit_lohi[ch] = 1; }
        else { g_io.pit_divisor[ch] = (uint16_t)((g_io.pit_divisor[ch] & 0x00FF) | (v << 8)); g_io.pit_lohi[ch] = 0; }
        return; }
    /* OPL2: AdLib ports (driver base in [DS:218C]) and the SB FM mirror */
    case 0x388: case 0x228: g_io.opl_index = v; return;
    case 0x389: case 0x229: audio_opl_write(g_io.opl_index, v); return;
    case 0x226:
        if (v & 1) g_dsp.reset = 1;
        else if (g_dsp.reset) { g_dsp.reset = 0; g_dsp.r = g_dsp.w = 0; g_dsp.need = 0; dsp_put(0xAA); }
        return;
    case 0x22C: dsp_write(v); return;
    case 0x38A: g_io.opl_index = (uint16_t)(0x100 | v); return;      /* OPL3 bank 1 */
    case 0x38B: audio_opl_write(g_io.opl_index, v); return;
    default:
        /* SB DSP (22xh) and DMA are replaced at the CT-VOICE driver level,
         * not emulated at port level - see RECOMP_PLAN.md */
        g_io.unhandled++;
        return;
    }
}

void port_out16(uint16_t port, uint16_t v)
{
    if (is_vga(port)) {
        if (g_io.trace) { g_io.trace(1, port, (uint8_t)v); g_io.trace(1, (uint16_t)(port + 1), (uint8_t)(v >> 8)); }
        vga_port_out16(&g_vga, port, v);
        return;
    }
    port_out8(port, (uint8_t)v);
    port_out8((uint16_t)(port + 1), (uint8_t)(v >> 8));
}

static uint8_t in8(uint16_t port)
{
    if (is_vga(port)) return vga_port_in8(&g_vga, port);
    switch (port) {
    case 0x21: return g_io.pic_imr;
    case 0x60: return g_io.kbd_data;
    case 0x61: return g_io.port61;
    case 0x64: return 0x14;                              /* KBC status: input buffer empty */
    case 0x226: return 0xFF;
    case 0x22A: { uint8_t v = 0xAA; if (g_dsp.r != g_dsp.w) { v = g_dsp.q[g_dsp.r]; g_dsp.r = (g_dsp.r + 1) % 8; } return v; }
    case 0x22C: return 0x00;                              /* write buffer ready (bit 7 = 0) */
    case 0x22E: return (uint8_t)(g_dsp.r != g_dsp.w ? 0xFF : 0x7F);  /* bit 7: data available; read = IRQ ack */
    case 0x330: return 0xFE;                              /* MPU-401 data ACK */
    case 0x331: return 0x00;                              /* MPU-401 status: ready */
    case 0x388: case 0x228: return audio_opl_status();
    default: g_io.unhandled++; return 0xFF;
    }
}

uint8_t port_in8(uint16_t port)
{
    uint8_t v = in8(port);
    if (g_io.trace) g_io.trace(0, port, v);
    return v;
}

uint16_t port_in16(uint16_t port)
{
    return (uint16_t)(port_in8(port) | (port_in8((uint16_t)(port + 1)) << 8));
}
