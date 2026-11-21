#ifndef FOX_LAUNCHER_CONFIG_H
#define FOX_LAUNCHER_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char game_dir[512];
    char exe_path[512];
    int  scale;                  /* 1, 2, 3, 4, 5 */
    int  aspect_4_3;             /* 1 = 4:3 CRT, 0 = 16:10 */
    int  fullscreen;             /* 0 = windowed, 1 = fullscreen */
    int  sfx_mode;               /* 0 = hybrid, 1 = purist pc speaker, 2 = adlib */
    int  autofire;               /* 1 = on, 0 = off */
    int  infinite_shields;       /* 1 = on, 0 = off */
    int  language;               /* 0 = English, 1 = Korean */
    int  music_type;             /* 0 = OPL3, 1 = Arranged, 2 = MT-32, 3 = SC-55 */
    int  volume_master;          /* 0 - 100 */
    int  volume_music;           /* 0 - 100 */
    int  volume_sfx;             /* 0 - 100 */
    int  volume_voice;           /* 0 - 100 */
    /* Fox Ranger II */
    int  music_device;           /* FR2 sound pack: 0 = A (AdLib), 1 = I (PC speaker), 2 = M (MT-32), 3 = S (SC-55) */
    int  voice;                  /* 1 = Sound Blaster speech ("v") */
    int  skip_intro;             /* 1 = start at the title screen */
    int  stage_fade;             /* 1 = fade to black when a stage hands off */
} LauncherConfig;

/* Settings file name and per-user folder (default "foxranger.ini", "FoxRanger");
 * call before load/save to keep another game's settings apart. */
void launcher_config_use(const char *ini_name, const char *app_dir);

void launcher_config_defaults(LauncherConfig *cfg);
int  launcher_config_load(LauncherConfig *cfg);
int  launcher_config_save(const LauncherConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* FOX_LAUNCHER_CONFIG_H */
