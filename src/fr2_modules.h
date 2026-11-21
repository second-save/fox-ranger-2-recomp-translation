/* fr2_modules.h - Fox Ranger II program table and runner.
 *
 * Fox Ranger II is 28 DOS programs.  FR2.EXE is a hub: it reads FR.REF,
 * applies the sound/voice arguments and execl()s the next program for a
 * route letter; every other program does its job and execl()s
 * "fr2.exe a v <letter>" back.  The runner reproduces that chain:
 *
 *   - each program is loaded with mz_load() exactly like DOS (PSP, image,
 *     relocations) and started the way its MSC 6 crt0 would start it
 *     (DS = SS = DGROUP, crt0's stack adjustment, BSS cleared, main(argc,
 *     argv, envp)) - the facts are parsed from each program's own startup
 *     code by tools/crt_ident.py;
 *   - C run-time calls go to native shims chosen by name (identified per
 *     program by instruction-level matching, also tools/crt_ident.py);
 *   - FR2.EXE itself is small-model hub code; its route table is extracted
 *     from the binary by tools/fr2_dispatch_gen.py and executed natively.
 */
#ifndef FOX_FR2_MODULES_H
#define FOX_FR2_MODULES_H
#include <stdint.h>
#include <stddef.h>

typedef struct { uint16_t off; const char *name; } Fr2CrtEntry;

typedef struct {
    const char *name;            /* "st1", "pro3", ...                         */
    uint16_t crt_seg;            /* image segment of crt0 / the C run-time      */
    uint16_t dgroup;             /* image segment of DGROUP (crt0 `mov di,`)    */
    uint16_t hdr_ss, hdr_sp;     /* MZ header initial stack                     */
    uint16_t sp_add;             /* crt0 `add sp,` after switching SS to DGROUP */
    uint16_t bss_lo, bss_hi;     /* DGROUP offsets cleared by crt0              */
    uint16_t main_seg, main_off; /* image address of main()                     */
    const Fr2CrtEntry *crt;      /* run-time entry points the game calls        */
} Fr2Module;

extern const Fr2Module g_fr2_modules[];

/* FR2.EXE route table: argv lists for execl (NULL-terminated).  A first
 * element "@13" means "set video mode 13h first"; "$4" means "argv[4]". */
typedef struct { const char *const *normal, *const *s_variant; } Fr2Route;
extern const Fr2Route g_fr2_routes[26];

const Fr2Module *fr2_module_find(const char *name);

/* Run the game starting with `fr2 <args>` (as FRII.BAT does: "fr2 A").
 * Returns the DOS exit code of the last program. */
int fr2_run(int argc, char **argv);

/* Intro skipping: with fr2_skip_intro set, a new game starts at the title
 * screen (route N) instead of the PRO1-PRO4 intro; fr2_intro_skip_key()
 * (Esc/Space during the intro) jumps there at once. */
extern int fr2_skip_intro;
extern int fr2_stage_fade;       /* 1 = fade to black when a stage hands off */
int  fr2_intro_skip_key(void);    /* 1 if the key was used to skip */
int  fr2_pause_key(void);         /* P in a stage: toggle pause; 1 if used */

/* Run the chain starting at an arbitrary program (debugging: "st1 a v V"). */
int fr2_run_program(const char *prog, int argc, char **argv);

#endif
