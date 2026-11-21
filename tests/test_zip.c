/* test_zip - extract a PKZIP 1.x archive with src/zipread.c.
 * usage: test_zip ARCHIVE DEST_DIR   (compare DEST_DIR with `unzip -d`) */
#include "zipread.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s ARCHIVE DEST_DIR\n", argv[0]); return 2; }
    char err[256];
    if (zip_extract_all(argv[1], argv[2], err, sizeof err)) { fprintf(stderr, "FAIL %s: %s\n", argv[1], err); return 1; }
    return 0;
}
