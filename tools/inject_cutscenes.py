#!/usr/bin/env python3
"""inject_cutscenes.py - Inject English cutscene translations into Fox Ranger II binaries.

Takes translations/cutscenes_kr.json (with 'english' fields populated),
patches the far pointer tables and appends new string blocks to:
  PRO3_UNP.EXE, PRO4_UNP.EXE, VIS1_UNP2.EXE, VIS2_UNP2.EXE,
  VIS4_UNP2.EXE, VIS5_UNP2.EXE, and EPIL_UNP2.EXE.

Validates that all injected strings match bit-for-bit upon re-reading the
patched binaries, and outputs 36-character terminal preview text cards.
"""
import argparse
import json
import os
import struct
import sys

BOX_WIDTH = 36


def format_card(item_id, module, text, kor_text=None):
    top = "+" + "-" * (BOX_WIDTH + 2) + "+"
    hdr = f"| [{item_id}] ({module})".ljust(BOX_WIDTH + 3) + "|"
    body = f"| {text.ljust(BOX_WIDTH)} |"
    bot = "+" + "-" * (BOX_WIDTH + 2) + "+"
    lines = [top, hdr, body]
    if kor_text:
        lines.append(f"| (KR: {kor_text})".ljust(BOX_WIDTH + 3) + "|")
    lines.append(bot)
    return "\n".join(lines)


def inject_module(exe_path, items, zero_old=True):
    if not os.path.exists(exe_path):
        print(f"Error: {exe_path} not found")
        return False

    data = bytearray(open(exe_path, 'rb').read())
    hdr_paras = struct.unpack_from('<H', data, 8)[0]
    hdr_bytes = hdr_paras * 16
    pages = struct.unpack_from('<H', data, 4)[0]
    last = struct.unpack_from('<H', data, 2)[0]
    old_file_size = pages * 512 - (512 - last if last else 0)
    old_img_size = old_file_size - hdr_bytes

    # Paragraph align the appended string segment
    pad_bytes = (16 - (old_img_size % 16)) % 16
    new_seg = (old_img_size + pad_bytes) // 16

    str_buf = bytearray()
    ptr_updates = []

    for item in items:
        en_str = item.get('english', '').strip()
        if not en_str:
            en_str = item.get('korean', '')

        str_off = len(str_buf)
        encoded = en_str.encode('ascii', errors='replace') + b'\0'
        str_buf.extend(encoded)

        ptr_file = int(item['ptr_file'], 16)
        ptr_updates.append((ptr_file, str_off, new_seg, en_str))

        # Optionally zero out old Korean string bytes
        if zero_old:
            target_file = int(item['target_file'], 16)
            max_bytes = item['max_bytes']
            for i in range(max_bytes):
                if target_file + i < len(data):
                    data[target_file + i] = 0

    # Truncate to old_file_size in case file had trailing garbage
    data = data[:old_file_size]
    # Append padding and new strings
    data.extend(b'\0' * pad_bytes)
    data.extend(str_buf)

    # Patch far pointer table entries
    for ptr_file, str_off, seg_val, _ in ptr_updates:
        struct.pack_into('<HH', data, ptr_file, str_off, seg_val)

    # Update MZ header file size
    new_file_size = len(data)
    new_pages = (new_file_size + 511) // 512
    new_last = new_file_size % 512
    struct.pack_into('<HH', data, 2, new_last, new_pages)

    open(exe_path, 'wb').write(data)
    return True


def verify_module(exe_path, items):
    data = open(exe_path, 'rb').read()
    hdr_paras = struct.unpack_from('<H', data, 8)[0]
    hdr_bytes = hdr_paras * 16

    errors = 0
    verified = []
    for item in items:
        ptr_file = int(item['ptr_file'], 16)
        off, seg = struct.unpack_from('<HH', data, ptr_file)
        lin = hdr_bytes + seg * 16 + off
        end = data.find(b'\0', lin)
        read_str = data[lin:end].decode('ascii', errors='replace')

        expected = item.get('english', '').strip()
        if not expected:
            expected = item.get('korean', '').strip()

        if read_str != expected:
            print(f"Mismatch in {item['id']}! Expected: '{expected}', Read: '{read_str}'")
            errors += 1
        else:
            verified.append((item['id'], item['module'], read_str, item.get('korean', '')))

    return errors == 0, verified


def main():
    parser = argparse.ArgumentParser(description="Inject English translations into Fox Ranger II cutscene binaries")
    parser.add_argument('--json', default='translations/cutscenes_kr.json', help='Cutscenes JSON file')
    parser.add_argument('--work', default='work', help='Work directory with unpacked executables')
    parser.add_argument('--preview', action='store_true', default=True, help='Print preview text cards')
    args = parser.parse_args()

    if not os.path.exists(args.json):
        sys.exit(f"File not found: {args.json}")

    with open(args.json, 'r', encoding='utf-8') as f:
        cutscenes = json.load(f)

    by_module = {}
    for item in cutscenes:
        by_module.setdefault(item['module'], []).append(item)

    print(f"=== Injecting Cutscenes for Fox Ranger II ({len(cutscenes)} strings across {len(by_module)} modules) ===")

    total_verified = 0
    preview_cards = []

    for mod_name, items in by_module.items():
        # Identify binary path
        exe_path = os.path.join(args.work, f"{mod_name}_UNP2.EXE")
        if not os.path.exists(exe_path):
            exe_path = os.path.join(args.work, f"{mod_name}_UNP.EXE")

        print(f"\nProcessing {mod_name} -> {exe_path} ({len(items)} strings)...")
        if not inject_module(exe_path, items):
            print(f"Failed to inject {mod_name}")
            continue

        ok, verified_list = verify_module(exe_path, items)
        if ok:
            print(f"  [PASS] Verified {len(verified_list)} strings in {os.path.basename(exe_path)}")
            total_verified += len(verified_list)
            for v in verified_list:
                preview_cards.append(v)
        else:
            print(f"  [FAIL] Verification errors in {mod_name}!")

    print(f"\nSuccessfully injected and verified {total_verified} / {len(cutscenes)} cutscene strings.")

    if args.preview:
        print("\n=== Cutscene Dialog Preview Cards (36-character width) ===")
        # Print sample cards from each module
        seen_mod = set()
        for item_id, mod, en_txt, kr_txt in preview_cards:
            if mod not in seen_mod:
                print(f"\n--- {mod} Sample Cards ---")
                seen_mod.add(mod)
                # Print first 2 cards of each module
                mod_cards = [c for c in preview_cards if c[1] == mod]
                for card in mod_cards[:2]:
                    print(format_card(card[0], card[1], card[2], card[3]))


if __name__ == '__main__':
    main()
