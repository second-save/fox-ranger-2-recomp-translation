/* zipread.h - extract the FR2 installer archives (*.ZZZ = PKZIP 1.x .zip,
 * methods "stored", "shrunk" and "imploded"), so the launcher needs no unzip tool. */
#ifndef FOX_ZIPREAD_H
#define FOX_ZIPREAD_H
#include <stddef.h>

/* Extract every file of `zip` into `dst_dir` (flat, names as stored).
 * 0 = ok; -1 = error, `err` says why.  CRC-checked. */
int zip_extract_all(const char *zip, const char *dst_dir, char *err, size_t errsz);

#endif
