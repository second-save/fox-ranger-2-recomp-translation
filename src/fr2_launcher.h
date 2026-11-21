/* fr2_launcher.h - Fox Ranger II launcher and game-data import.
 *
 * The player points the launcher at the original DOS release: the installer
 * folder (INSTALL.EXE, 1-6/E/V.ZZZ data archives, A/I/M/S.ZZZ music packs)
 * or an already installed game.  Installer archives are unpacked once into
 * the per-user data folder; each music pack keeps its own sub-folder
 * (packs/A, packs/I, ...) because all four use the same file names, and the
 * chosen one is copied in before the game starts (the original installer
 * copied only one). */
#ifndef FOX_FR2_LAUNCHER_H
#define FOX_FR2_LAUNCHER_H

#include "launcher.h"
#include <stddef.h>

int  fr2_data_dir(char *out, size_t n);                 /* per-user import folder, created; 0 = ok */
int  fr2_data_ready(const char *dir);                   /* 1 if dir holds a playable FR2 */
int  fr2_pack_present(const char *dir, int device);     /* 1 if packs/<A|I|M|S> was imported */
int  fr2_select_music(const char *dir, int device);     /* copy the pack in; 0 = ok (or no packs) */
/* Import from an installer or installed folder; on success cfg->game_dir is
 * the folder to play from.  Returns 0 = ok, -1 = failed (msg says why). */
int  fr2_import(const char *src, LauncherConfig *cfg, char *msg, size_t msgsz);

LauncherResult fr2_launcher_run(LauncherConfig *cfg);
int  fr2_launcher_snapshot(const char *ppm_path, const LauncherConfig *cfg);  /* headless, for docs */

#endif
