/* pklite.h - native unpacker for the FR2 programs (PKLITE 1.12, then EXEPACK).
 *
 * Output is laid out like tools/unpack_exe.py (which emulates the stub):
 * image with trailing zeros trimmed to a paragraph, relocations sorted as
 * seg = addr >> 4 / off = addr & 15, min-alloc from the original SS:SP, so
 * program data, relocations and entry match work/pristine/ (tests/test_pklite.c).
 * shortcut: the emulator copy also keeps the stub's leftover packed bytes past
 * the program's data (stack/free area); here that area is zero, which only
 * moves the heap start.  Reproduce it if a program ever reads it. */
#ifndef FOX_PKLITE_H
#define FOX_PKLITE_H
#include <stddef.h>
#include <stdint.h>

/* Unpack PKLITE 1.12 and Microsoft EXEPACK layers (FR2 uses both, nested).
 * 0 = ok (*out malloc'ed MZ file), 1 = not packed (use as is), -1 = corrupt */
int exe_unpack(const uint8_t *in, size_t n, uint8_t **out, size_t *outn);

#endif
