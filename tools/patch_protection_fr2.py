#!/usr/bin/env python3
"""patch_protection_fr2.py - disable Fox Ranger II copy-protection check.

In Fox Ranger II: Second Mission (1993, Soft Action):
1. FR2.EXE / FOX2.EXE (The Multi-Process Dispatcher):
   Stage 1 finishes by invoking: fr2.exe a v M
   Case 'M' at image offset 0x050c calls:
       spawnl("pwd.exe", "pwd.exe", "L", "s", "C", NULL)
   The jump table for all 26 cases ('A'-'Z') resides at image 0x0580.
   Case 'M' is at image 0x0580 + (ord('M') - ord('A')) * 2 = 0x0598.
   Case 'I' (image 0x0494) is the post-protection stage handler (launches vis1.exe).

   Patching Case 'M' in FR2_UNP.EXE / FOX2_UNP.EXE:
     img 0x0598: 0c 05 -> 94 04  (targets Case 'I' at 0x0494 directly, skipping PWD.EXE)

2. PWD_UNP2.EXE (The Password Entry Binary):
   When launched, PWD prompts for manual code coordinates from PWD.DAT.
   On valid entry, at image 0x0589 it calls:
       spawnl("fr2.exe", "fr2.exe", "I", "r", "L", NULL)
       exit(0)
   The prompt routine begins at image 0x0549 (file 0x1029):
     img 0x0549: 55 8b ec 81 ec 00 00  (push bp; mov bp, sp; sub sp, 0)
   Patching PWD_UNP2.EXE:
     img 0x0549: e9 3d 00 90 90 90 90  (jmp 0x0589; nop*4)
   This causes PWD to immediately spawn fr2.exe with Case 'I' and exit cleanly.
"""
import os
import struct
import sys

# Patches for FR2_UNP.EXE and FOX2_UNP.EXE
# Header is 0x40 bytes.
FR2_PATCHES = [
    (0x0598, bytes.fromhex('0C05'), bytes.fromhex('9404'), "Case 'M' (pwd.exe) -> Case 'I' (vis1.exe)"),
]

# Patches for PWD_UNP2.EXE
# Header is 0xAE0 bytes.
PWD_PATCHES = [
    (0x0549, bytes.fromhex('558BEC81EC0000'), bytes.fromhex('E93D0090909090'), "Bypass prompt -> jmp 0x0589 (spawn fr2.exe I r L)"),
]


def patch_file(path, patches, expected_hdr_paras=None):
    if not os.path.exists(path):
        print(f"Skipping {path}: file not found")
        return False
    data = bytearray(open(path, 'rb').read())
    if data[:2] != b'MZ':
        print(f"Error: {path} is not an MZ executable")
        return False
    hdr_paras = struct.unpack_from('<H', data, 8)[0]
    hdr_bytes = hdr_paras * 16
    if expected_hdr_paras is not None and hdr_paras != expected_hdr_paras:
        print(f"Warning: {path} header size 0x{hdr_bytes:x} differs from expected 0x{expected_hdr_paras*16:x}")

    modified = False
    for img_off, old_bytes, new_bytes, desc in patches:
        file_off = hdr_bytes + img_off
        cur = bytes(data[file_off:file_off + len(old_bytes)])
        if cur == new_bytes:
            print(f"  {path} (img 0x{img_off:04x}): already patched ({desc})")
            continue
        if cur != old_bytes:
            print(f"  {path} (img 0x{img_off:04x}): byte mismatch! expected {old_bytes.hex()}, found {cur.hex()}")
            return False
        data[file_off:file_off + len(new_bytes)] = new_bytes
        print(f"  {path} (img 0x{img_off:04x}, file 0x{file_off:06x}): {old_bytes.hex()} -> {new_bytes.hex()} ({desc})")
        modified = True

    if modified:
        open(path, 'wb').write(data)
        print(f"Successfully patched {path}")
    return True


def main():
    target_files = [
        ('work/FR2_UNP.EXE', FR2_PATCHES, 0x04),
        ('work/FOX2_UNP.EXE', FR2_PATCHES, 0x04),
        ('work/PWD_UNP2.EXE', PWD_PATCHES, 0xAE),
    ]

    if len(sys.argv) > 1:
        target = sys.argv[1]
        base = os.path.basename(target).upper()
        if 'PWD' in base:
            patch_file(target, PWD_PATCHES, None)
        else:
            patch_file(target, FR2_PATCHES, None)
        return

    print("=== Patching Fox Ranger 2 Copy Protection Checks ===")
    for path, patches, hdr in target_files:
        patch_file(path, patches, hdr)


if __name__ == '__main__':
    main()
