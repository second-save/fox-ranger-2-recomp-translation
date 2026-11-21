#include <stdio.h>
#include <assert.h>
#include "mz_load.h"
#include "mem.h"

static int mock_st1_entry(int argc, char **argv)
{
    printf("mock_st1_entry called! argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = '%s'\n", i, argv[i]);
    }
    return 42;
}

int main(void)
{
    printf("Testing fr_ref_state_t layout and size...\n");
    assert(sizeof(fr_ref_state_t) == 38);
    printf("sizeof(fr_ref_state_t) = %zu [PASS]\n", sizeof(fr_ref_state_t));

    /* Test loading extracted FR.REF */
    int res = fr_ref_load("work/extracted/FR.REF");
    assert(res == 0);
    printf("Loaded work/extracted/FR.REF: sound=%d, voice=%d, lives=%d, diff=%d [PASS]\n",
           g_fr_ref.sound_device, g_fr_ref.voice_enabled, g_fr_ref.lives, g_fr_ref.difficulty);
    assert(g_fr_ref.sound_device == 2);
    assert(g_fr_ref.voice_enabled == 2);
    assert(g_fr_ref.lives == 3);
    assert(g_fr_ref.difficulty == 1);

    /* Test registering a stage handler */
    register_stage_entry("st1", mock_st1_entry);

    /* Test exec_stage on ST1_UNP.EXE */
    char *mock_argv[] = { "st1.exe", "a", "v", "V", NULL };
    int status = exec_stage("ST1", 4, mock_argv);
    printf("exec_stage return code: %d [PASS]\n", status);
    assert(status == 42);

    printf("All stage router tests passed successfully!\n");
    return 0;
}
