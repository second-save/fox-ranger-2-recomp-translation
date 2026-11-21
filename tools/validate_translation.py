#!/usr/bin/env python3
"""validate_translation.py - check translations/cutscenes_kr.json against the
pristine (unpacked, un-injected) executables.

For every entry the far pointer at ptr_img must currently hold seg:off, its
segment word must be covered by an MZ relocation, seg:off must equal
target_img, and the CRC-32 of the original string there (up to its NUL) must
equal `korean_crc32` (the Korean script itself is not kept in the repo).
Usage: validate_translation.py PRISTINE_DIR
"""
import json, os, struct, sys, zlib


def load(p):
    d = open(p, 'rb').read()
    h = struct.unpack_from('<H', d, 8)[0] * 16
    last, pages, nrel = struct.unpack_from('<HHH', d, 2)
    size = pages * 512 - (512 - last if last else 0)
    ro = struct.unpack_from('<H', d, 0x18)[0]
    rel = set()
    for i in range(nrel):
        o, s = struct.unpack_from('<HH', d, ro + 4 * i)
        rel.add(s * 16 + o)
    return d[h:size], rel


def main():
    pdir = sys.argv[1]
    items = json.load(open('translations/cutscenes_kr.json', encoding='utf-8'))
    bad = []
    cache = {}
    for x in items:
        m = x['module']
        if m not in cache:
            cache[m] = load(os.path.join(pdir, f'{m}.EXE'))
        img, rel = cache[m]
        p = int(x['ptr_img'], 16); t = int(x['target_img'], 16)
        seg, off = int(x['seg'], 16), int(x['off'], 16)
        o, s = struct.unpack_from('<HH', img, p)
        why = []
        if (s, o) != (seg, off): why.append(f'ptr holds {s:04X}:{o:04X} not {seg:04X}:{off:04X}')
        if p + 2 not in rel: why.append('segment word not relocated')
        if p in rel: why.append('offset word is relocated (not a far pointer: segment slots?)')
        if seg * 16 + off != t: why.append('seg:off != target')
        raw = img[t:img.index(b'\0', t)]
        if zlib.crc32(raw) != int(x['korean_crc32'], 16): why.append('original string differs (CRC-32)')
        if why:
            bad.append((x['id'], why))
    for i, w in bad:
        print(i, '; '.join(w))
    print(f'{len(items) - len(bad)}/{len(items)} entries valid')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
