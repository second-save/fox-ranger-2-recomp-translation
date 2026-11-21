/* main_fr2.c - Fox Ranger II: Second Mission.
 *
 * Without options it opens the launcher (fr2_launcher.c), which finds the
 * game files and settings; command-line runs (--run, --headless, ...) skip
 * it and use work/extracted unless --game is given.  Runs the original program chain exactly as FRII.BAT does ("fr2 A"): FR2.EXE
 * routes to the intro, title, stages and cutscenes, every program execl()s
 * back to it.  See fr2_modules.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "audio.h"
#include "cheat.h"
#include "fr2_launcher.h"
#include "cpu.h"
#include "crt_shims.h"
#include "dos.h"
#include "fr2_modules.h"
#include "io.h"
#include "mem.h"
#include "platform.h"
#include "recomp_rt.h"
#include "vga_modex.h"
#include <SDL.h>

static jmp_buf g_exit_jb;
static int g_exit_code;

/* FR2_CALLHIST=1: histogram of far calls, dumped at exit */
#define CH_N 8192
static struct { uint32_t key; const char *name; uint64_t n; } g_ch[CH_N];
static int g_ch_on;
/* FR2_WATCH=PTR,OFF: report when the dword at ([DS:PTR]):OFF changes
 * (checked at every far call; DS = the running program's DGROUP) */
static int g_watch_on; static uint16_t g_watch_ptr, g_watch_off;
static void watch_check(Cpu *c, uint16_t seg, uint16_t off, const char *name)
{
    static uint32_t prev; static int init; static uint16_t pseg, poff; static const char *pname;
    uint16_t ws = rd16(c->ds, g_watch_ptr);
    uint32_t cur = rd16(ws, g_watch_off) | ((uint32_t)rd16(ws, (uint16_t)(g_watch_off + 2)) << 16);
    if (init && cur != prev)
        fprintf(stderr, "WATCH %04X:%04X %08X -> %08X between %04X:%04X(%s) and %04X:%04X(%s)\n", ws, g_watch_off,
                prev, cur, pseg, poff, pname ? pname : "", seg, off, name ? name : "");
    prev = cur; init = 1; pseg = seg; poff = off; pname = name;
}
static void ch_log(Cpu *c, uint16_t seg, uint16_t off, const char *name)
{
    if (g_watch_on) watch_check(c, seg, off, name);
    if (!g_ch_on) return;
    uint32_t key = ((uint32_t)seg << 16) | off;
    for (uint32_t i = (key * 2654435761u) % CH_N, n = 0; n < CH_N; i = (i + 1) % CH_N, n++) {
        if (g_ch[i].n && g_ch[i].key != key) continue;
        g_ch[i].key = key; g_ch[i].name = name; g_ch[i].n++; return;
    }
}
static int ch_cmp(const void *a, const void *b)
{
    const __typeof__(g_ch[0]) *x = a, *y = b;
    return x->n < y->n ? 1 : x->n > y->n ? -1 : 0;
}
static void ch_dump(void)
{
    if (!g_ch_on) return;
    qsort(g_ch, CH_N, sizeof g_ch[0], ch_cmp);
    fprintf(stderr, "---- far call histogram ----\n");
    for (int i = 0; i < CH_N && g_ch[i].n; i++)
        fprintf(stderr, "%10llu  %04X:%04X  %s\n", (unsigned long long)g_ch[i].n,
                g_ch[i].key >> 16, g_ch[i].key & 0xFFFF, g_ch[i].name ? g_ch[i].name : "");
}

static void on_exit_request(int code)
{
    ch_dump();
    g_exit_code = code;
    longjmp(g_exit_jb, 1);
}

/* FR2_TRACE_PORTS=1: log sound-related port accesses (SB 220-22F, OPL 388-38B, MPU 330/331) */
static void port_trace(int out, uint16_t port, uint8_t v)
{
    if ((port >= 0x220 && port <= 0x22F) || (port >= 0x388 && port <= 0x38B && !out) || port == 0x330 || port == 0x331 ||
        (port >= 0x00 && port <= 0x0F) || (port >= 0x80 && port <= 0x8F))
    {
        fprintf(stderr, "%s %03X %02X\n", out ? "OUT" : "IN ", port, v);
        static int stacks;
        if (out && port == 0x226 && stacks++ < 2) rt_shadow_print();
    }
}

static int host_key(int sc)
{
    if (sc == SDL_SCANCODE_P) return fr2_pause_key();
    return (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_SPACE) ? fr2_intro_skip_key() : 0;
}

static int getch_hook(Cpu *c) { return platform_bios_getkey(c); }

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Fox Ranger II: Second Mission - native port\n"
        "\n"
        "Usage: %s [options]\n"
        "  (no options)      open the launcher\n"
        "  --no-launcher     start the game directly with the options below\n"
        "  --game DIR        game files (default: launcher setting, else work/extracted)\n"
        "  --launcher-shot F render the launcher to F.ppm and exit (docs)\n"
        "  --sound X         FR2 music device: A (AdLib/Sound Blaster FM, default)\n"
        "                    (M and S drive MIDI on an MPU-401, which is not emulated)\n"
        "  --skip-intro      start at the title screen (Esc/Space also skips the intro)\n"
        "  --no-stage-fade   hard cut when a stage ends, as the original on a fast PC\n"
        "  --no-voice        no Sound Blaster speech (as the shipped FRII.BAT: \"fr2 A\")\n"
        "  --run PROG ARGS.. start at a program instead of FR2.EXE, e.g.\n"
        "                    --run st1 a v V      (stage 1)\n"
        "                    --run fr2 a v N      (title screen route)\n"
        "  --scale N         window scale (default 3)\n"
        "  --fullscreen\n"
        "  --verbose\n"
        "  --headless --seconds S --shots T:file.ppm,... --keys T:KEY,... --record DIR\n"
        "\n"
        "Debug environment: FR2_TRACE_EXEC=1, FR2_CALLHIST=1\n", prog);
}

int main(int argc, char **argv)
{
    PlatformOpts po = { 0 };
    po.scale = 3;
    po.aspect_4_3 = 1;
    const char *sound = "A";
    int voice = 1;
    int run_argc = 0;
    char **run_argv = NULL;
    int no_launcher = 0;
    const char *game = NULL, *launcher_shot = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { print_usage(argv[0]); return 0; }
        else if (!strcmp(a, "--no-launcher")) no_launcher = 1;
        else if (!strcmp(a, "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(a, "--launcher-shot") && i + 1 < argc) launcher_shot = argv[++i];
        else if (!strcmp(a, "--sound") && i + 1 < argc) sound = argv[++i];
        else if (!strcmp(a, "--no-voice")) voice = 0;
        else if (!strcmp(a, "--skip-intro")) fr2_skip_intro = 1;
        else if (!strcmp(a, "--no-stage-fade")) fr2_stage_fade = 0;
        else if (!strcmp(a, "--scale") && i + 1 < argc) po.scale = atoi(argv[++i]);
        else if (!strcmp(a, "--fullscreen")) po.fullscreen = 1;
        else if (!strcmp(a, "--verbose")) po.verbose = 1;
        else if (!strcmp(a, "--headless")) po.headless = 1;
        else if (!strcmp(a, "--seconds") && i + 1 < argc) po.seconds = atof(argv[++i]);
        else if (!strcmp(a, "--shots") && i + 1 < argc) po.shots = argv[++i];
        else if (!strcmp(a, "--keys") && i + 1 < argc) po.keys = argv[++i];
        else if (!strcmp(a, "--record") && i + 1 < argc) po.record_frames = argv[++i];
        else if (!strcmp(a, "--run") && i + 1 < argc) { run_argv = &argv[i + 1]; run_argc = argc - i - 1; break; }
        else { fprintf(stderr, "Unknown option: %s\n", a); print_usage(argv[0]); return 1; }
    }

    if (getenv("FR2_TRACE_PORTS")) g_io.trace = port_trace;
    if (getenv("FR2_CALLHIST")) { g_ch_on = 1; rt_call_log = ch_log; }
    if (getenv("FR2_WATCH") && sscanf(getenv("FR2_WATCH"), "%hx,%hx", &g_watch_ptr, &g_watch_off) == 2) {
        g_watch_on = 1; rt_call_log = ch_log;
    }

    LauncherConfig cfg;
    launcher_config_use("foxranger2.ini", "FoxRanger2");    /* keep FR1's foxranger.ini apart */
    launcher_config_load(&cfg);
    if (launcher_shot) {
        if (game) snprintf(cfg.game_dir, sizeof cfg.game_dir, "%s", game);
        return fr2_launcher_snapshot(launcher_shot, &cfg) ? 1 : 0;
    }
    int scripted = po.headless || po.keys || po.shots || po.record_frames || run_argc;
    if (!scripted && !no_launcher) {
        if (game) snprintf(cfg.game_dir, sizeof cfg.game_dir, "%s", game);
        if (fr2_launcher_run(&cfg) != LAUNCHER_START_GAME) return 0;
        static const char *dev[] = { "A", "I", "M", "S" };
        game = cfg.game_dir;
        if (fr2_select_music(game, cfg.music_device & 3)) fprintf(stderr, "fr2: could not switch the music pack\n");
        sound = dev[cfg.music_device & 3];
        voice = cfg.voice;
        fr2_skip_intro = cfg.skip_intro;
        fr2_stage_fade = cfg.stage_fade;
        po.scale = cfg.scale;
        po.fullscreen = cfg.fullscreen;
        po.aspect_4_3 = cfg.aspect_4_3;
        po.autofire = cfg.autofire;
        cheat_set_infinite_shield(cfg.infinite_shields);
        audio_set_master_volume(cfg.volume_master);
        audio_set_music_volume(cfg.volume_music);
        audio_set_sfx_volume(cfg.volume_music);     /* PC speaker = the music in mode I */
        audio_set_voice_volume(cfg.volume_voice);
    }
    dos_init(game ? game : "work/extracted");
    mem_reset();
    vga_reset(&g_vga);

    if (platform_init(&po)) { fprintf(stderr, "Failed to initialize platform\n"); return 1; }
    if (audio_init(AUDIO_RATE_DEFAULT, !po.headless) && !po.headless)
        fprintf(stderr, "audio: no output device, continuing silently\n");
    platform_install();
    platform_set_exit(on_exit_request);
    dos_exit_hook = on_exit_request;
    crt_getch_hook = getch_hook;
    platform_set_key_hook(host_key);

    if (setjmp(g_exit_jb) == 0) {
        if (run_argc) {
            g_exit_code = fr2_run_program(run_argv[0], run_argc, run_argv);
        } else {
            /* FR2 <sound> [v]: "v" turns on the Sound Blaster speech (FR.REF voice = 2) */
            char *fr2_argv[] = { "fr2", (char *)sound, voice ? "v" : NULL, NULL };
            g_exit_code = fr2_run(voice ? 3 : 2, fr2_argv);
        }
        ch_dump();
    }

    if (po.verbose) {
        PlatformStats st = platform_stats();
        fprintf(stderr, "exit %d: %llu frames, %llu timer IRQs, %llu keyboard IRQs\n", g_exit_code,
                (unsigned long long)st.frames, (unsigned long long)st.irq0, (unsigned long long)st.irq1);
        AudioStats as = audio_stats();
        fprintf(stderr, "audio: %u opl writes, %u voice clips, %u voice frames\n", as.opl_writes, as.voice_clips, as.voice_frames_out);
    }
    platform_shutdown();
    audio_shutdown();
    return g_exit_code & 0xFF;
}

#if defined(_WIN32) && !defined(_MSC_VER)
/* GUI-subsystem entry (-mwindows): no console window behind the game */
#include <stdlib.h>
int __stdcall WinMain(void *inst, void *prev, char *cmd, int show)
{
    (void)inst; (void)prev; (void)cmd; (void)show;
    return main(__argc, __argv);
}
#endif
