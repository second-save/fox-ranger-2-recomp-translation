/* fr2_launcher.c - Fox Ranger II launcher: green and yellow like the FR2
 * fighter, game-data import from the original DOS release, settings. */
#include "fr2_launcher.h"
#include "fr2_modules.h"
#include "pklite.h"
#include "zipread.h"
#include "native_dialog.h"
#include "retro_font.h"
#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>          /* not <io.h>: src/io.h shadows it */
#define mkdir_one(p) _mkdir(p)
#else
#include <dirent.h>
#include <limits.h>
#define mkdir_one(p) mkdir(p, 0755)
#endif

#define CANV_W 640
#define CANV_H 480

/* ------------------------------------------------------------ files */

static const char PACKS[4] = { 'A', 'I', 'M', 'S' };

static int exists(const char *dir, const char *name)
{
    char p[1024], lower[64];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (!stat(p, &st)) return 1;
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof lower; i++) lower[i] = (char)tolower((unsigned char)name[i]);
    lower[i] = 0;
    snprintf(p, sizeof p, "%s/%s", dir, lower);
    return !stat(p, &st);
}

static void mkdirs(const char *path)
{
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/' || *s == '\\') { char c = *s; *s = 0; mkdir_one(p); *s = c; }
    mkdir_one(p);
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return -1;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[65536];
    size_t n;
    int err = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { err = -1; break; }
    fclose(in);
    if (fclose(out)) err = -1;
    return err;
}

int fr2_data_dir(char *out, size_t n)
{
    const char *home = getenv("HOME");
#if defined(__APPLE__)
    if (!home) return -1;
    snprintf(out, n, "%s/Library/Application Support/FoxRanger2/game", home);
#elif defined(_WIN32)
    const char *ad = getenv("APPDATA");
    if (!ad) return -1;
    snprintf(out, n, "%s\\FoxRanger2\\game", ad);
#else
    if (!home) return -1;
    snprintf(out, n, "%s/.local/share/foxranger2/game", home);
#endif
    mkdirs(out);
    return 0;
}

int fr2_data_ready(const char *dir)
{
    static const char *need[] = { "FR2.EXE", "ST1.EXE", "ST63.EXE", "FOX2.DAT", "FOX2.FON", "STAGE1.DAT", NULL };
    if (!dir || !dir[0]) return 0;
    for (int i = 0; need[i]; i++) if (!exists(dir, need[i])) return 0;
    char un[1024];
    snprintf(un, sizeof un, "%s/unpacked", dir);
    return exists(un, "ST1.EXE") && exists(un, "ST63.EXE");
}

int fr2_pack_present(const char *dir, int device)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/packs/%c", dir, PACKS[device & 3]);
    return exists(p, "STAGE1.DAT");
}

int fr2_select_music(const char *dir, int device)
{
    static const char *files[] = { "BONUS.DAT", "BOSS.DAT", "CLEAR.DAT", "E1.DAT", "E2.DAT", "GAMEOVER.DAT",
        "P0.DAT", "P1.DAT", "P2.DAT", "P3.DAT", "SCORE.DAT", "STAGE1.DAT", "STAGE2.DAT", "STAGE3.DAT",
        "STAGE4.DAT", "STAGE5.DAT", "STAGE6.DAT", "TALK.DAT", NULL };
    if (!fr2_pack_present(dir, device)) return 0;     /* installed folder: keep its pack */
    for (int i = 0; files[i]; i++) {
        char from[1024], to[1024];
        snprintf(from, sizeof from, "%s/packs/%c/%s", dir, PACKS[device & 3], files[i]);
        snprintf(to, sizeof to, "%s/%s", dir, files[i]);
        if (copy_file(from, to)) return -1;
    }
    return 0;
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = len > 0 ? malloc((size_t)len) : NULL;
    if (d && fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); d = NULL; }
    fclose(f);
    *n = (size_t)len;
    return d;
}

/* the original programs are PKLITE+EXEPACK packed; the runtime loads the
 * unpacked images from <dir>/unpacked (fr2_run.c find_exe) */
static int unpack_programs(const char *dir, char *msg, size_t msgsz)
{
    char out_dir[1024];
    snprintf(out_dir, sizeof out_dir, "%s/unpacked", dir);
    mkdirs(out_dir);
    for (const Fr2Module *m = g_fr2_modules; m->name; m++) {
        char up[16], from[1024], to[1024];
        size_t k = 0, n, on;
        for (; m->name[k] && k + 1 < sizeof up; k++) up[k] = (char)toupper((unsigned char)m->name[k]);
        up[k] = 0;
        snprintf(from, sizeof from, "%s/%s.EXE", dir, up);
        snprintf(to, sizeof to, "%s/%s.EXE", out_dir, up);
        uint8_t *d = slurp(from, &n), *o = NULL;
        if (!d) { snprintf(msg, msgsz, "Missing %s.EXE.", up); return -1; }
        int r = exe_unpack(d, n, &o, &on);
        FILE *f = r < 0 ? NULL : fopen(to, "wb");
        int ok = f && fwrite(r == 0 ? o : d, 1, r == 0 ? on : n, f) == (r == 0 ? on : n);
        if (f && fclose(f)) ok = 0;
        free(d); free(o);
        if (!ok) { snprintf(msg, msgsz, "Could not unpack %s.EXE.", up); return -1; }
    }
    return 0;
}

/* installed DOS game: copy it, and file its music as the pack FRII.BAT names */
/* copy src/name to dst/NAME (upper case, as the DOS game expects); regular files only */
static int copy_one(const char *src, const char *dst, const char *name)
{
    char from[1024], to[1024], up[256];
    struct stat st;
    snprintf(from, sizeof from, "%s/%s", src, name);
    if (stat(from, &st) || !(st.st_mode & S_IFREG)) return 0;
    size_t k = 0;
    for (; name[k] && k + 1 < sizeof up; k++) up[k] = (char)toupper((unsigned char)name[k]);
    up[k] = 0;
    snprintf(to, sizeof to, "%s/%s", dst, up);
    return copy_file(from, to);
}

static int copy_installed(const char *src, const char *dst, char *msg, size_t msgsz)
{
#ifdef _WIN32
    char pat[1024];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", src);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { snprintf(msg, msgsz, "Cannot read that folder."); return -1; }
    do {
        if (copy_one(src, dst, fd.cFileName)) { FindClose(h); snprintf(msg, msgsz, "Could not copy %s.", fd.cFileName); return -1; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(src);
    if (!d) { snprintf(msg, msgsz, "Cannot read that folder."); return -1; }
    struct dirent *e;
    while ((e = readdir(d)))
        if (copy_one(src, dst, e->d_name)) { closedir(d); snprintf(msg, msgsz, "Could not copy %s.", e->d_name); return -1; }
    closedir(d);
#endif
    char bat[1024], line[64] = "", pdir[1024];
    snprintf(bat, sizeof bat, "%s/FRII.BAT", dst);
    FILE *f = fopen(bat, "r");
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    char dev = 'A';
    for (char *p = line; *p; p++) if (*p == ' ' && p[1]) { dev = (char)toupper((unsigned char)p[1]); break; }
    if (!strchr("AIMS", dev)) dev = 'A';
    snprintf(pdir, sizeof pdir, "%s/packs/%c", dst, dev);
    mkdirs(pdir);
    static const char *music[] = { "BONUS.DAT", "BOSS.DAT", "CLEAR.DAT", "E1.DAT", "E2.DAT", "GAMEOVER.DAT",
        "P0.DAT", "P1.DAT", "P2.DAT", "P3.DAT", "SCORE.DAT", "STAGE1.DAT", "STAGE2.DAT", "STAGE3.DAT",
        "STAGE4.DAT", "STAGE5.DAT", "STAGE6.DAT", "TALK.DAT", NULL };
    for (int i = 0; music[i]; i++) {
        char from[1024], to[1024];
        snprintf(from, sizeof from, "%s/%s", dst, music[i]);
        snprintf(to, sizeof to, "%s/%s", pdir, music[i]);
        copy_file(from, to);            /* P0.DAT is not in every pack */
    }
    return 0;
}

static int same_dir(const char *a, const char *b)
{
#ifdef _WIN32
    char ra[1024], rb[1024];
    return _fullpath(ra, a, sizeof ra) && _fullpath(rb, b, sizeof rb) && !_stricmp(ra, rb);
#else
    char ra[PATH_MAX], rb[PATH_MAX];
    return realpath(a, ra) && realpath(b, rb) && !strcmp(ra, rb);
#endif
}

int fr2_import(const char *src, LauncherConfig *cfg, char *msg, size_t msgsz)
{
    char dst[1024], zip[1024], name[16];
    if (fr2_data_dir(dst, sizeof dst)) { snprintf(msg, msgsz, "No user data folder."); return -1; }
    if (!exists(src, "1.ZZZ")) {
        static const char *need[] = { "FR2.EXE", "ST1.EXE", "ST63.EXE", "FOX2.DAT", "FOX2.FON", "STAGE1.DAT", NULL };
        for (int i = 0; need[i]; i++)
            if (!exists(src, need[i])) {
                snprintf(msg, msgsz, "No Fox Ranger II files there (need INSTALL.EXE + *.ZZZ).");
                return -1;
            }
        if (!same_dir(src, dst) && copy_installed(src, dst, msg, msgsz)) return -1;   /* never copy onto itself */
        if (unpack_programs(dst, msg, msgsz)) return -1;
        snprintf(cfg->game_dir, sizeof cfg->game_dir, "%s", dst);
        snprintf(msg, msgsz, "Installed game imported - ready for launch.");
        return 0;
    }
    static const char *data[] = { "1", "2", "3", "4", "5", "6", "E", "V", NULL };
    for (int i = 0; data[i]; i++) {
        snprintf(name, sizeof name, "%s.ZZZ", data[i]);
        snprintf(zip, sizeof zip, "%s/%s", src, name);
        char err[200];
        if (!exists(src, name) || zip_extract_all(zip, dst, err, sizeof err)) {
            snprintf(msg, msgsz, "Could not unpack %s: %s", name, exists(src, name) ? err : "missing");
            return -1;
        }
    }
    for (int i = 0; i < 4; i++) {
        char pdir[1024];
        snprintf(name, sizeof name, "%c.ZZZ", PACKS[i]);
        snprintf(zip, sizeof zip, "%s/%s", src, name);
        snprintf(pdir, sizeof pdir, "%s/packs/%c", dst, PACKS[i]);
        mkdirs(pdir);
        char err[200];
        if (exists(src, name) && zip_extract_all(zip, pdir, err, sizeof err)) {
            snprintf(msg, msgsz, "Could not unpack %s: %s", name, err);
            return -1;
        }
    }
    static const char *loose[] = { "FOX2.FON", "FR.REF", NULL };
    for (int i = 0; loose[i]; i++) {
        char from[1024], to[1024];
        snprintf(from, sizeof from, "%s/%s", src, loose[i]);
        snprintf(to, sizeof to, "%s/%s", dst, loose[i]);
        if (copy_file(from, to)) { snprintf(msg, msgsz, "Missing %s in the game folder.", loose[i]); return -1; }
    }
    if (unpack_programs(dst, msg, msgsz)) return -1;
    if (fr2_select_music(dst, 0) || !fr2_data_ready(dst)) { snprintf(msg, msgsz, "Import incomplete."); return -1; }
    snprintf(cfg->game_dir, sizeof cfg->game_dir, "%s", dst);
    snprintf(msg, msgsz, "Game imported - ready for launch.");
    return 0;
}

/* ------------------------------------------------------------ theme */

#define C_BG       0xFF030D06
#define C_FRAME    0xFF3FBF3F
#define C_PANEL_BD 0xFF2E8B3A
#define C_PANEL_BG 0xEE07200C
#define C_TITLE    0xFFFFD21E
#define C_TITLE_SH 0xFF3A2A00
#define C_HEAD     0xFFFFE04A
#define C_LABEL    0xFFB8F0A0
#define C_DIM      0xFF5E8A5A
#define C_OK       0xFF7CFF5A
#define C_BAD      0xFFFF7A4A
#define C_BAR      0xFFFFD21E

static const uint32_t g_theme[9] = {
    0xFF0A2410, 0xFF2E8B3A, 0xFFA8E890,     /* normal   */
    0xFF143A18, 0xFFFFE04A, 0xFFFFFFFF,     /* hovered  */
    0xFF2E5A0E, 0xFFFFC800, 0xFFFFF060,     /* selected */
};

/* original pixel art of a green/yellow FR2-style fighter */
static const char *SHIP[] = {
    "..........W..........",
    ".........YWY.........",
    ".........YGY.........",
    "........gGCGg........",
    "........gGCGg........",
    ".......gGGCGGg.......",
    ".......gGGGGGg.......",
    "...Y...gGGYGGg...Y...",
    "...Y..gGGGYGGGg..Y...",
    "..YG..gGGGYGGGg..GY..",
    "..YG.gGGGGGGGGGg.GY..",
    "..YGgGGGGGGGGGGGgGY..",
    ".YGGGGGGYGGGYGGGGGGY.",
    "YGGGGGGYYGGGYYGGGGGGY",
    "YGGGGGYYgGGGgYYGGGGGY",
    "yYYYYYy.gGGGg.yYYYYYy",
    "........gGGGg........",
    ".......yYgGgYy.......",
    ".......yY...Yy.......",
};

static void put(uint32_t *fb, int x, int y, uint32_t c)
{
    if (x >= 0 && x < CANV_W && y >= 0 && y < CANV_H) fb[y * CANV_W + x] = c;
}

static void draw_ship(uint32_t *fb, int x0, int y0, int s, int frame)
{
    int rows = (int)(sizeof SHIP / sizeof *SHIP);
    for (int r = 0; r < rows; r++)
        for (int c = 0; SHIP[r][c]; c++) {
            uint32_t col;
            switch (SHIP[r][c]) {
            case 'G': col = 0xFF3FCF3F; break;
            case 'g': col = 0xFF1E7A2A; break;
            case 'Y': col = 0xFFFFD21E; break;
            case 'y': col = 0xFFC08A10; break;
            case 'C': col = 0xFF6FE8FF; break;
            case 'W': col = 0xFFFFFFFF; break;
            default: continue;
            }
            for (int dy = 0; dy < s; dy++) for (int dx = 0; dx < s; dx++) put(fb, x0 + c * s + dx, y0 + r * s + dy, col);
        }
    /* engine flames, flickering */
    int len = 2 + (frame / 3) % 3;
    for (int i = 0; i < len; i++) {
        uint32_t col = i == 0 ? 0xFFFFFFA0 : i == 1 ? 0xFFFFC800 : 0xFFFF6A00;
        for (int e = 0; e < 2; e++)
            for (int dy = 0; dy < s; dy++) for (int dx = 0; dx < s; dx++)
                put(fb, x0 + (8 + e * 4) * s + dx, y0 + (rows + i) * s + dy, col);
    }
}

#define STARS 70
typedef struct { int x, y, sp; } Star;

static void stars_step(uint32_t *fb, Star *st, int move)
{
    static const uint32_t col[3] = { 0xFF1E5A2A, 0xFF6FBF5A, 0xFFE8FFB0 };
    for (int i = 0; i < STARS; i++) {
        if (move && (st[i].y += st[i].sp) >= CANV_H) { st[i].y = 0; st[i].x = rand() % CANV_W; }
        put(fb, st[i].x, st[i].y, col[st[i].sp - 1]);
    }
}

/* ------------------------------------------------------------ layout */

enum {
    B_NONE, B_IMPORT, B_MUS_A, B_MUS_I, B_MUS_M, B_MUS_S, B_VOICE_ON, B_VOICE_OFF,
    B_VM_DN, B_VM_UP, B_VMU_DN, B_VMU_UP, B_VV_DN, B_VV_UP,
    B_S1, B_S2, B_S3, B_S4, B_SFULL, B_A43, B_A1610, B_AF_ON, B_AF_OFF, B_SH_N, B_SH_I,
    B_IN_PLAY, B_IN_SKIP, B_FADE_ON, B_FADE_OFF, B_QUIT, B_START
};
typedef struct { int x, y, w, h; const char *label; int id; } Btn;

typedef struct { int ready; char msg[160]; int frame; } UiState;

static int build_buttons(Btn *b, const LauncherConfig *cfg, const UiState *ui)
{
    int n = 0;
#define B(X, Y, W, H, L, ID) b[n++] = (Btn){ X, Y, W, H, L, ID }
    B(420, 102, 190, 20, ui->ready ? "CHANGE GAME FOLDER" : "SELECT GAME FOLDER...", B_IMPORT);
    B(130, 184, 100, 20, "ADLIB FM", B_MUS_A);
    B(236, 184, 110, 20, "PC SPEAKER", B_MUS_I);
    B(352, 184, 125, 20, "MT-32 (MIDI)", B_MUS_M);
    B(483, 184, 125, 20, "SC-55 (MIDI)", B_MUS_S);
    B(130, 208, 160, 20, "SOUND BLASTER ON", B_VOICE_ON);
    B(296, 208, 60, 20, "OFF", B_VOICE_OFF);
    B( 96, 234, 22, 18, "-", B_VM_DN);  B(156, 234, 22, 18, "+", B_VM_UP);
    B(296, 234, 22, 18, "-", B_VMU_DN); B(356, 234, 22, 18, "+", B_VMU_UP);
    B(496, 234, 22, 18, "-", B_VV_DN);  B(556, 234, 22, 18, "+", B_VV_UP);
    B(130, 290, 40, 20, "1x", B_S1); B(175, 290, 40, 20, "2x", B_S2);
    B(220, 290, 40, 20, "3x", B_S3); B(265, 290, 40, 20, "4x", B_S4);
    B(310, 290, 60, 20, "FULL", B_SFULL);
    B(450, 290, 80, 20, "4:3 CRT", B_A43); B(536, 290, 72, 20, "16:10", B_A1610);
    B(130, 314, 190, 20, "RAPID AUTOFIRE", B_AF_ON); B(326, 314, 130, 20, "ORIGINAL FIRE", B_AF_OFF);
    B(130, 338, 140, 20, "NORMAL", B_SH_N); B(276, 338, 200, 20, "INFINITE (CHEAT)", B_SH_I);
    B(130, 362, 140, 20, "PLAY INTRO", B_IN_PLAY); B(276, 362, 140, 20, "SKIP INTRO", B_IN_SKIP);
    B(130, 386, 140, 20, "FADE OUT", B_FADE_ON); B(276, 386, 200, 20, "HARD CUT (ORIGINAL)", B_FADE_OFF);
    B(20, 420, 140, 34, "QUIT (ESC)", B_QUIT);
    B(175, 420, 445, 34, ui->ready ? ">>> LAUNCH MISSION (ENTER) <<<" : "[ GAME FILES REQUIRED ]", B_START);
#undef B
    (void)cfg;
    return n;
}

static int is_selected(int id, const LauncherConfig *c, const UiState *ui)
{
    switch (id) {
    case B_MUS_A: return c->music_device == 0;
    case B_MUS_I: return c->music_device == 1;
    case B_MUS_M: return c->music_device == 2;
    case B_MUS_S: return c->music_device == 3;
    case B_VOICE_ON: return c->voice;
    case B_VOICE_OFF: return !c->voice;
    case B_S1: case B_S2: case B_S3: case B_S4: return !c->fullscreen && c->scale == id - B_S1 + 1;
    case B_SFULL: return c->fullscreen;
    case B_A43: return c->aspect_4_3;
    case B_A1610: return !c->aspect_4_3;
    case B_AF_ON: return c->autofire;
    case B_AF_OFF: return !c->autofire;
    case B_SH_N: return !c->infinite_shields;
    case B_SH_I: return c->infinite_shields;
    case B_IN_PLAY: return !c->skip_intro;
    case B_IN_SKIP: return c->skip_intro;
    case B_FADE_ON: return c->stage_fade;
    case B_FADE_OFF: return !c->stage_fade;
    case B_START: return ui->ready && (ui->frame / 20) % 2 == 0;
    default: return 0;
    }
}

static void vol_bar(uint32_t *fb, int x, int y, int v)
{
    char s[8];
    snprintf(s, sizeof s, "%3d%%", v);
    retro_draw_text(fb, CANV_W, CANV_H, x + 26, y + 1, s, 0xFFFFFFFF, 0);
}

static void draw(uint32_t *fb, const LauncherConfig *c, const UiState *ui, const Btn *b, int nb, int hov, Star *st)
{
    retro_fill_rect(fb, CANV_W, CANV_H, 0, 0, CANV_W, CANV_H, C_BG);
    stars_step(fb, st, 1);
    retro_draw_panel(fb, CANV_W, CANV_H, 8, 6, CANV_W - 16, CANV_H - 12, C_FRAME, 0);

    retro_draw_text_2x(fb, CANV_W, CANV_H, 28, 12, "FOX RANGER II", C_TITLE, C_TITLE_SH);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 44, "SECOND MISSION * 1993 SOFT ACTION * NATIVE PORT", C_LABEL, 0);
    draw_ship(fb, 548, 6, 3, ui->frame);

    /* game data */
    retro_draw_panel(fb, CANV_W, CANV_H, 20, 64, 600, 88, C_PANEL_BD, C_PANEL_BG);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 68, "GAME FILES", C_HEAD, 0);
    char line[120];
    size_t gl = strlen(c->game_dir);
    if (!gl) snprintf(line, sizeof line, "[ not set: select the original DOS folder ]");
    else if (gl > 72) snprintf(line, sizeof line, "...%s", c->game_dir + gl - 69);   /* keep the tail */
    else snprintf(line, sizeof line, "%s", c->game_dir);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 86, line, c->game_dir[0] ? 0xFFFFFFFF : C_BAD, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 104, "MUSIC PACKS:", C_LABEL, 0);
    for (int i = 0; i < 4; i++) {
        char t[8];
        int ok = ui->ready && fr2_pack_present(c->game_dir, i);
        snprintf(t, sizeof t, "[%c]", PACKS[i]);
        retro_draw_text(fb, CANV_W, CANV_H, 134 + i * 34, 104, t, ok ? C_OK : C_DIM, 0);
    }
    retro_draw_text(fb, CANV_W, CANV_H, 30, 128, ui->msg, ui->ready ? C_OK : C_TITLE, 0);

    /* audio */
    retro_draw_panel(fb, CANV_W, CANV_H, 20, 160, 600, 100, C_PANEL_BD, C_PANEL_BG);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 164, "SOUND", C_HEAD, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 186, "Music:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 210, "Speech:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 362, 210, "MIDI devices: not yet emulated", C_DIM, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 235, "Master", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 240, 235, "Music", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 440, 235, "Voice", C_LABEL, 0);
    vol_bar(fb, 96, 234, c->volume_master);
    vol_bar(fb, 296, 234, c->volume_music);
    vol_bar(fb, 496, 234, c->volume_voice);

    /* video & gameplay */
    retro_draw_panel(fb, CANV_W, CANV_H, 20, 268, 600, 144, C_PANEL_BD, C_PANEL_BG);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 272, "VIDEO & GAMEPLAY", C_HEAD, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 292, "Window:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 390, 292, "Aspect:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 316, "Fire:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 340, "Shield:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 364, "Intro:", C_LABEL, 0);
    retro_draw_text(fb, CANV_W, CANV_H, 30, 388, "Stage end:", C_LABEL, 0);

    for (int i = 0; i < nb; i++) {
        int disabled = (b[i].id == B_MUS_M || b[i].id == B_MUS_S);
        retro_draw_button(fb, CANV_W, CANV_H, b[i].x, b[i].y, b[i].w, b[i].h, b[i].label,
                          !disabled && hov == b[i].id, is_selected(b[i].id, c, ui));
    }
    retro_draw_text(fb, CANV_W, CANV_H, 36, 458, "In game: P pause * Konami code: infinite shield * Esc skips intro", C_DIM, 0);
}

/* ------------------------------------------------------------ run */

static int clampv(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

static void refresh(LauncherConfig *cfg, UiState *ui)
{
    ui->ready = fr2_data_ready(cfg->game_dir);
    if (ui->ready && !ui->msg[0]) snprintf(ui->msg, sizeof ui->msg, "Game files OK - ready for launch.");
    if (!ui->ready && !ui->msg[0])
        snprintf(ui->msg, sizeof ui->msg, "Select or drop your Fox Ranger II DOS folder (INSTALL.EXE + *.ZZZ).");
}

static void import_folder(const char *sel, LauncherConfig *cfg, UiState *ui, SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb)
{
    /* unpacking takes a few seconds: say so before blocking */
    retro_draw_panel(fb, CANV_W, CANV_H, 120, 200, 400, 60, C_TITLE, 0xFF0A2410);
    retro_draw_text(fb, CANV_W, CANV_H, 200, 222, "IMPORTING GAME FILES...", C_TITLE, 0);
    SDL_UpdateTexture(tex, NULL, fb, CANV_W * 4);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, NULL, NULL); SDL_RenderPresent(ren);
    ui->msg[0] = 0;
    fr2_import(sel, cfg, ui->msg, sizeof ui->msg);
    refresh(cfg, ui);
    launcher_config_save(cfg);
}

static void choose_folder(LauncherConfig *cfg, UiState *ui, SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb)
{
    char sel[512] = "";
    if (native_dialog_browse_folder("Select the Fox Ranger II DOS folder", sel, sizeof sel)) {
        snprintf(ui->msg, sizeof ui->msg, "No folder chosen - you can also drop it on this window.");
        return;
    }
    import_folder(sel, cfg, ui, ren, tex, fb);
}

static void click(int id, LauncherConfig *c)
{
    switch (id) {
    case B_MUS_A: c->music_device = 0; break;
    case B_MUS_I: c->music_device = 1; break;
    case B_VOICE_ON: c->voice = 1; break;
    case B_VOICE_OFF: c->voice = 0; break;
    case B_VM_DN: c->volume_master = clampv(c->volume_master - 10); break;
    case B_VM_UP: c->volume_master = clampv(c->volume_master + 10); break;
    case B_VMU_DN: c->volume_music = clampv(c->volume_music - 10); break;
    case B_VMU_UP: c->volume_music = clampv(c->volume_music + 10); break;
    case B_VV_DN: c->volume_voice = clampv(c->volume_voice - 10); break;
    case B_VV_UP: c->volume_voice = clampv(c->volume_voice + 10); break;
    case B_S1: case B_S2: case B_S3: case B_S4: c->scale = id - B_S1 + 1; c->fullscreen = 0; break;
    case B_SFULL: c->fullscreen = 1; break;
    case B_A43: c->aspect_4_3 = 1; break;
    case B_A1610: c->aspect_4_3 = 0; break;
    case B_AF_ON: c->autofire = 1; break;
    case B_AF_OFF: c->autofire = 0; break;
    case B_SH_N: c->infinite_shields = 0; break;
    case B_SH_I: c->infinite_shields = 1; break;
    case B_IN_PLAY: c->skip_intro = 0; break;
    case B_IN_SKIP: c->skip_intro = 1; break;
    case B_FADE_ON: c->stage_fade = 1; break;
    case B_FADE_OFF: c->stage_fade = 0; break;
    default: return;
    }
    launcher_config_save(c);
}

LauncherResult fr2_launcher_run(LauncherConfig *cfg)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) return LAUNCHER_ERROR;
    SDL_Window *win = SDL_CreateWindow("Fox Ranger II - Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       CANV_W * 2, CANV_H * 2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) return LAUNCHER_ERROR;
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CANV_W, CANV_H) : NULL;
    uint32_t *fb = calloc(CANV_W * CANV_H, sizeof *fb);
    if (!tex || !fb) {
        free(fb);
        if (tex) SDL_DestroyTexture(tex);
        if (ren) SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return LAUNCHER_ERROR;
    }
    SDL_RenderSetLogicalSize(ren, CANV_W, CANV_H);
    retro_set_button_theme(g_theme);

    Star st[STARS];
    for (int i = 0; i < STARS; i++) st[i] = (Star){ rand() % CANV_W, rand() % CANV_H, 1 + rand() % 3 };
    UiState ui = { 0 };
    refresh(cfg, &ui);

    LauncherResult res = LAUNCHER_QUIT;
    int running = 1, mx = -1, my = -1;
    Btn b[40];
    while (running) {
        int nb = build_buttons(b, cfg, &ui), hov = B_NONE;
        for (int i = 0; i < nb; i++)
            if (mx >= b[i].x && mx < b[i].x + b[i].w && my >= b[i].y && my < b[i].y + b[i].h) hov = b[i].id;
        int act = B_NONE;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_MOUSEMOTION) { mx = e.motion.x; my = e.motion.y; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) act = hov;
            else if (e.type == SDL_DROPFILE) {                /* folder dropped on the window */
                import_folder(e.drop.file, cfg, &ui, ren, tex, fb);
                SDL_free(e.drop.file);
            }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) act = B_START;
            }
        }
        if (act == B_QUIT) running = 0;
        else if (act == B_IMPORT || (act == B_START && !ui.ready)) choose_folder(cfg, &ui, ren, tex, fb);
        else if (act == B_START) { launcher_config_save(cfg); res = LAUNCHER_START_GAME; running = 0; }
        else click(act, cfg);

        ui.frame++;
        draw(fb, cfg, &ui, b, nb, hov, st);
        SDL_UpdateTexture(tex, NULL, fb, CANV_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    retro_set_button_theme(NULL);
    free(fb);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return res;
}

int fr2_launcher_snapshot(const char *ppm_path, const LauncherConfig *cfg)
{
    uint32_t *fb = calloc(CANV_W * CANV_H, sizeof *fb);
    if (!fb) return -1;
    srand(1993);
    Star st[STARS];
    for (int i = 0; i < STARS; i++) st[i] = (Star){ rand() % CANV_W, rand() % CANV_H, 1 + rand() % 3 };
    UiState ui = { 0 };
    LauncherConfig c = *cfg;
    refresh(&c, &ui);
    Btn b[40];
    int nb = build_buttons(b, &c, &ui);
    retro_set_button_theme(g_theme);
    draw(fb, &c, &ui, b, nb, B_START, st);
    retro_set_button_theme(NULL);
    FILE *f = fopen(ppm_path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", CANV_W, CANV_H);
        for (int i = 0; i < CANV_W * CANV_H; i++) {
            uint8_t rgb[3] = { (uint8_t)(fb[i] >> 16), (uint8_t)(fb[i] >> 8), (uint8_t)fb[i] };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    free(fb);
    return f ? 0 : -1;
}
