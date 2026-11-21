#ifndef FOX_MZ_LOAD_H
#define FOX_MZ_LOAD_H
#include <stdint.h>

#pragma pack(push, 1)
/* 38-byte binary layout of FR.REF shared between FR2.EXE and stage modules */
typedef struct {
    uint8_t  sound_device;      /* +0x00: 1=PC Spk, 2=AdLib, 3=MIDI, 4=SB */
    uint8_t  voice_enabled;     /* +0x01: 0=off, 1=music only, 2=CT-VOICE on */
    uint8_t  reserved02;        /* +0x02: padding / reserved */
    uint8_t  lives;             /* +0x03: Player lives (default 3) */
    uint8_t  stage_id;          /* +0x04: Current stage identifier / letter */
    uint8_t  sub_stage;         /* +0x05: Sub-stage index / letter */
    uint16_t weapon_level;      /* +0x06: Primary weapon power level */
    uint16_t sub_weapon;        /* +0x08: Secondary weapon selection */
    uint16_t speed;             /* +0x0A: Ship speed level */
    uint8_t  inventory[21];     /* +0x0C: Score / powerup state / flags (0x0C..0x20) */
    uint8_t  difficulty;        /* +0x21: Difficulty / loop counter (default 1) */
    uint8_t  padding[4];        /* +0x22: Trailing alignment bytes (0x22..0x25) */
} fr_ref_state_t;
#pragma pack(pop)

_Static_assert(sizeof(fr_ref_state_t) == 38, "fr_ref_state_t must be exactly 38 bytes");

extern fr_ref_state_t g_fr_ref;

/* Load/save FR.REF binary state file (38 bytes) */
int  fr_ref_load(const char *path);
int  fr_ref_save(const char *path);
void fr_ref_init_default(void);

typedef struct {
    uint16_t psp_seg, load_seg;
    uint16_t cs, ip, ss, sp;        /* relocated entry state */
    uint32_t image_size;
    uint16_t nrelocs;
    uint16_t end_seg;               /* first paragraph after image + minalloc (heap start) */
} MzInfo;

/* Load an (unpacked) MZ executable into ram[] at load_seg exactly like DOS:
 * PSP at load_seg-10h with the command tail, image copied, relocations
 * patched (+load_seg). Sets g_load_seg. Returns 0 on success. */
int mz_load(const char *path, uint16_t load_seg, const char *cmdtail, MzInfo *out);

#endif
