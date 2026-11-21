/* test_fr2_import - import the original FR2 installer folder and switch music packs.
 * usage: HOME=<scratch> test_fr2_import <folder with INSTALL.EXE + *.ZZZ>
 * (uses $HOME for the data folder: point it somewhere disposable) */
#include "fr2_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* not assert(): release builds define NDEBUG */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* cheap content fingerprint */
static unsigned long sum(const char *dir, const char *name)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    unsigned long h = 5381;
    for (int ch; (ch = fgetc(f)) != EOF;) h = h * 33 + (unsigned)ch;
    fclose(f);
    return h;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s INSTALLER_DIR\n", argv[0]); return 2; }
    LauncherConfig cfg;
    launcher_config_defaults(&cfg);
    char msg[160];

    CHECK(fr2_import("/nonexistent", &cfg, msg, sizeof msg) == -1);
    int r = fr2_import(argv[1], &cfg, msg, sizeof msg);
    printf("import: %d %s -> %s\n", r, msg, cfg.game_dir);
    CHECK(r == 0 && fr2_data_ready(cfg.game_dir));
    for (int d = 0; d < 4; d++) CHECK(fr2_pack_present(cfg.game_dir, d));

    /* each pack has its own STAGE1.DAT: switching must change the file in play */
    unsigned long a = sum(cfg.game_dir, "STAGE1.DAT");
    CHECK(fr2_select_music(cfg.game_dir, 1) == 0);
    unsigned long i = sum(cfg.game_dir, "STAGE1.DAT");
    CHECK(fr2_select_music(cfg.game_dir, 0) == 0);
    CHECK(a && i && a != i && sum(cfg.game_dir, "STAGE1.DAT") == a);
    CHECK(i == sum(cfg.game_dir, "packs/I/STAGE1.DAT"));

    /* programs were unpacked next to the data */
    CHECK(sum(cfg.game_dir, "unpacked/ST1.EXE") && sum(cfg.game_dir, "unpacked/VIS5.EXE"));

    /* picking the import folder itself must not destroy it */
    LauncherConfig c2;
    launcher_config_defaults(&c2);
    unsigned long st1 = sum(cfg.game_dir, "ST1.EXE");
    CHECK(fr2_import(cfg.game_dir, &c2, msg, sizeof msg) == 0 && !strcmp(c2.game_dir, cfg.game_dir));
    CHECK(sum(cfg.game_dir, "ST1.EXE") == st1 && fr2_data_ready(cfg.game_dir));
    puts("ok");
    return 0;
}
