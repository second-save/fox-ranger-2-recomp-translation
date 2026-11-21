/* main_sdl.c - boots the recompiled Fox Ranger.
 *
 * Loads the (unpacked, translated) EXE image into emulated memory like DOS,
 * installs the native layers (CRT shims, blitters, CT-VOICE, platform BIOS /
 * interrupts) and the recompiled code, then performs the work of the MSC
 * start-up code (134D:001A) natively and calls main() at 0000:0175:
 *   SS = DS = DGROUP, SP = 2CBEh (stack top after `add sp, 24BEh`),
 *   BSS DS:2174-24BF zeroed, main(argc, argv, envp) with argv from the
 *   command tail (default " a v" = AdLib music + voice, as in FR.BAT).
 *
 *   foxrt [--game DIR] [--exe FILE] [--args " a v"] [--scale N] [--verbose]
 *         [--headless] [--seconds S] [--keys "T:KEY,..."] [--shots "T:file.ppm,..."]
 *         [--wav out.wav]
 *
 * Controls: arrows move, D = gun (Ctrl), C = bomb/special (Alt), Space/Enter
 * select, Esc. The original Ctrl/Alt keys keep working. Scripted keys: ENTER
 * SPACE ESC UP DOWN LEFT RIGHT CTRL ALT SHIFT FIRE BOMB A-Z 0-9.
 *
 * The copy-protection check (04A4:14B0) is replaced natively (always passes),
 * and all text comes from the loaded image, so work/FOX_EN_FINAL.EXE gives
 * the English game.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "blit_10a7.h"
#include "cpu.h"
#include "crt_shims.h"
#include "ctvoice.h"
#include "dos.h"
#include "heap.h"
#include "mem.h"
#include "mz_load.h"
#include "natives.h"
#include "platform.h"
#include "cheat.h"
#include "recomp_rt.h"
#include "fr2_modules.h"
#include <ctype.h>
#if __has_include("recomp_gen.h")
#include "recomp_gen.h"
#else
static inline void recomp_register_all(void) {}
#endif
#include "vga_modex.h"
#include "launcher.h"
#include "launcher_config.h"
#include "ingest.h"
#include "enhanced_music.h"
#include <SDL.h>

#define LOAD_SEG   0x0800          /* PSP at 07F0h: ~620 KB conventional memory free */
#define DGROUP_IMG 0x1ADF
#define MAIN_SEG   0x0000
#define MAIN_OFF   0x0175

static jmp_buf g_exit_jb;
static FILE   *g_calls;
static void log_call(Cpu *c, uint16_t seg, uint16_t off, const char *name)
{
    (void)c;
    fprintf(g_calls, "%9.4f %04X:%04X %s\n", platform_now_us() / 1e6, seg, off, name ? name : "?");
    fflush(g_calls);
}
static int     g_exit_code;
static void on_exit_request(int code) { g_exit_code = code; longjmp(g_exit_jb, 1); }
static int host_key(int sc) { return (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_SPACE) ? fr2_intro_skip_key() : 0; }

int main(int argc, char **argv)
{
    const char *game = "fox-ranger", *exe = "work/FOX_EN_FINAL.EXE", *tail = " a v";
    (void)tail;
    const char *cli_game = NULL, *cli_exe = NULL;
    const char *start_stage = NULL;
    const char *cutscene_name = NULL;
    int cli_scale = 0;
    int no_launcher = 0;
    const char *record_launcher_dir = NULL;
    PlatformOpts po = { 0 };
    po.scale = 3;
    po.aspect_4_3 = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "%s needs a value\n", a), exit(2), ""))
        if (!strcmp(a, "--game")) { game = NEXT(); cli_game = game; }
        else if (!strcmp(a, "--exe")) { exe = NEXT(); cli_exe = exe; }
        else if (!strcmp(a, "--stage")) { start_stage = NEXT(); }
        else if (!strcmp(a, "--cutscene")) { cutscene_name = NEXT(); }
        else if (!strcmp(a, "--args")) tail = NEXT();
        else if (!strcmp(a, "--scale")) { po.scale = atoi(NEXT()); cli_scale = po.scale; }
        else if (!strcmp(a, "--no-launcher")) no_launcher = 1;
        else if (!strcmp(a, "--skip-intro")) fr2_skip_intro = 1;
        else if (!strcmp(a, "--record-launcher")) record_launcher_dir = NEXT();
        else if (!strcmp(a, "--headless")) po.headless = 1;
        else if (!strcmp(a, "--seconds")) po.seconds = atof(NEXT());
        else if (!strcmp(a, "--keys")) po.keys = NEXT();
        else if (!strcmp(a, "--shots")) po.shots = NEXT();
        else if (!strcmp(a, "--record-frames")) po.record_frames = NEXT();
        else if (!strcmp(a, "--record-fps")) po.record_fps = atof(NEXT());
        else if (!strcmp(a, "--wav")) po.wav = NEXT();
        else if (!strcmp(a, "--verbose")) po.verbose = 1;
        else if (!strcmp(a, "--trace-calls")) { g_calls = fopen(NEXT(), "w"); rt_call_log = g_calls ? log_call : NULL; }
        else {
            fprintf(stderr, "usage: %s [--game DIR] [--exe FILE] [--args \" a v\"] [--scale N] [--verbose]\n"
                            "       [--headless] [--seconds S] [--keys \"T:KEY,...\"] [--shots \"T:f.ppm,...\"]\n"
                            "       [--record-frames DIR] [--record-fps FPS] [--record-launcher DIR] [--wav f.wav]\n"
                            "       [--no-launcher]\n", argv[0]);
            return 2;
        }
    }

    if (record_launcher_dir) {
        return launcher_record_demo(record_launcher_dir, game);
    }

    LauncherConfig cfg;
    launcher_config_load(&cfg);

    int is_scripted = (po.headless || po.keys != NULL || po.shots != NULL || po.record_frames != NULL);
    if (!is_scripted && !no_launcher && !start_stage && !cutscene_name) {
        if (cli_game) snprintf(cfg.game_dir, sizeof(cfg.game_dir), "%s", cli_game);
        if (cli_exe) snprintf(cfg.exe_path, sizeof(cfg.exe_path), "%s", cli_exe);
        if (cli_scale > 0) cfg.scale = cli_scale;

        LauncherResult res = launcher_run(&cfg);
        if (res != LAUNCHER_START_GAME) {
            return 0;
        }
        game = cfg.game_dir;
        exe = cfg.exe_path;
        po.scale = cfg.scale;
        po.fullscreen = cfg.fullscreen;
        po.aspect_4_3 = cfg.aspect_4_3;
        po.autofire = cfg.autofire;
        cheat_set_infinite_shield(cfg.infinite_shields);
    }

    if (!po.headless && po.keys == NULL && po.shots == NULL) {
        /* FR2's M/S music modes drive MIDI on an MPU-401, which is not
         * emulated: keep AdLib FM music for every launcher choice */
        if (cfg.sfx_mode == 1) tail = " i v";
        else tail = " a v";
    }

    /* Enhanced soundtrack setup & volume levels */
    char enh_dir[512];
    snprintf(enh_dir, sizeof(enh_dir), "%s/enhanced_music", game);
    enhanced_music_init(enh_dir);
    enhanced_music_set_enabled(cfg.music_type == 1);

    audio_set_master_volume(cfg.volume_master);
    audio_set_music_volume(cfg.volume_music);
    audio_set_sfx_volume(cfg.volume_sfx);
    audio_set_voice_volume(cfg.volume_voice);

    /* Verify executable path with fallbacks */
    const char *fr2_candidates[] = {
        exe,
        "work/FR2_UNP.EXE",
        "work/FOX2_UNP.EXE",
        "work/extracted/FR2.EXE",
        "work/extracted/FOX2.EXE",
        NULL
    };
    for (int i = 0; fr2_candidates[i]; i++) {
        FILE *fc = fopen(fr2_candidates[i], "rb");
        if (fc) {
            fclose(fc);
            exe = fr2_candidates[i];
            break;
        }
    }

    mem_reset();
    vga_reset(&g_vga);

    (void)cutscene_name;
    dos_init("work/extracted");
    if (platform_init(&po)) return 1;
    if (audio_init(AUDIO_RATE_DEFAULT, !po.headless) && !po.headless)
        fprintf(stderr, "audio: no output device, continuing silently\n");
    platform_install();
    platform_set_exit(on_exit_request);
    dos_exit_hook = on_exit_request;
    platform_set_key_hook(host_key);

    if (setjmp(g_exit_jb) == 0) {
        if (start_stage) {
            /* debugging: start at a program, e.g. --stage st1 */
            char *stage_argv[] = { (char *)start_stage, "a", "v", "V", NULL };
            g_exit_code = fr2_run_program(start_stage, 4, stage_argv);
        } else {
            /* FRII.BAT: "fr2 A" - sound letter from the launcher's music choice */
            char snd[2] = { (char)toupper((unsigned char)(tail[1] ? tail[1] : 'a')), 0 };
            /* the tail's second letter ("v") enables Sound Blaster speech */
            char *fr2_argv[] = { "fr2", snd, (tail[1] && tail[2] && tail[3] == 'v') ? "v" : NULL, NULL };
            g_exit_code = fr2_run(fr2_argv[2] ? 3 : 2, fr2_argv);
        }
    }

    PlatformStats st = platform_stats();
    AudioStats ast = audio_stats();
    if (po.verbose) {
        fprintf(stderr, "exit %d after %.2f s: %llu timer IRQs, %llu keyboard IRQs, %llu frames, %llu idle waits, %llu polls\n",
                g_exit_code, platform_now_us() / 1e6, (unsigned long long)st.irq0, (unsigned long long)st.irq1,
                (unsigned long long)st.frames, (unsigned long long)st.idle_sleeps, (unsigned long long)st.polls);
        fprintf(stderr, "audio stats: %u opl writes, %u voice clips, %u voice frames\n",
                ast.opl_writes, ast.voice_clips, ast.voice_frames_out);
    }
    platform_shutdown();
    audio_shutdown();
    enhanced_music_shutdown();
    if (g_calls) fclose(g_calls);
    return g_exit_code & 0xFF;
}
