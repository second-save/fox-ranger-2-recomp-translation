/* fr2_translation.h - English text applied to the pristine programs at load
 * time (see tools/gen_translation_c.py). */
#ifndef FOX_FR2_TRANSLATION_H
#define FOX_FR2_TRANSLATION_H
#include <stdint.h>

typedef struct {
    const char *module;     /* "pro3", ...                                   */
    uint32_t ptr_img;       /* image offset of the far pointer               */
    uint16_t seg, off;      /* its original value (image-relative segment)   */
    const char *english;
} Fr2Text;

extern const Fr2Text g_fr2_texts[];

#endif
