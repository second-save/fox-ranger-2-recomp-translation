#!/usr/bin/env python3
"""dump_assets.py - unpack / view Fox Ranger (1992, Soft Action) data files.

usage: dump_assets.py GAME_DIR OUT_DIR [--exe UNPACKED_FOX.EXE]

  FOX.DAT      palette + 4 motion records + 8x8 / 8x16 ASCII fonts
               + Johab 16x16 jamo font (8 cho x19, 2 jung x21, 3 jong x27)
  VSPRITE.PAT  208 sprites, RLE, directory of (w,h,packed_size) at EOF
  STAGE.DAT    6 x 4096-byte stage records
  VOICE.DAT    Creative Voice File: block list
  --exe        unpacked FOX.EXE (tools/unpack_exe.py, run twice: PKLITE then
               EXEPACK) -> Johab script table with file offsets + byte budget
No third-party dependencies (PNG written with zlib).
"""
import json, os, struct, sys, zlib

# ------------------------------------------------------------------ PNG
def write_png(path, w, h, rows, palette=None):
    """rows: list of bytes (8-bit indices if palette else grey)."""
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    ctype = 3 if palette else 0
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0))
    if palette:
        out += chunk(b'PLTE', bytes(palette))
    out += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(out)


def glyph_sheet(path, glyphs, gw, gh, cols=16, scale=2):
    """glyphs: list of row-bitmasks (ints, MSB = left pixel)."""
    rows_n = (len(glyphs) + cols - 1) // cols
    W, H = cols * (gw + 1) * scale, rows_n * (gh + 1) * scale
    img = [bytearray([60]) * W for _ in range(H)]
    for k, g in enumerate(glyphs):
        ox, oy = (k % cols) * (gw + 1) * scale, (k // cols) * (gh + 1) * scale
        for y in range(gh):
            for x in range(gw):
                v = 255 if g[y] & (1 << (gw - 1 - x)) else 0
                for sy in range(scale):
                    for sx in range(scale):
                        img[oy + y * scale + sy][ox + x * scale + sx] = v
    write_png(path, W, H, img)


# --------------------------------------------------------------- FOX.DAT
FOX_LAYOUT = dict(palette=0x300, ascii8x8=(95, 8), ascii8x16=(95, 16),
                  cho=(8, 19), jung=(2, 21), jong=(3, 27))


def parse_fox_dat(d):
    """Mirrors the loader in FOX.EXE (image 0x11700..0x119d0)."""
    o, sec = 0, {}
    sec['palette'] = (o, d[o:o + 0x300]); o += 0x300
    recs = []
    for _ in range(4):                       # 4 x {u8 n; u8 a[2n]; u8 b[2n]}
        n = d[o]; sz = (n & 0x7F) * 2
        recs.append(dict(offset=o, n=n, a=d[o + 1:o + 1 + sz], b=d[o + 1 + sz:o + 1 + 2 * sz]))
        o += 1 + 2 * sz
    sec['records'] = recs
    sec['ascii8x8'] = (o, d[o:o + 0x2F8]); o += 0x2F8     # 95 glyphs, chars 0x20..0x7E
    sec['ascii8x16'] = (o, d[o:o + 0x5F0]); o += 0x5F0    # 95 glyphs, 8x16
    for name, (sets, n) in (('cho', (8, 19)), ('jung', (2, 21)), ('jong', (3, 27))):
        size = sets * n * 32
        sec[name] = (o, d[o:o + size]); o += size
    sec['end'] = o
    assert o == len(d), f'FOX.DAT layout mismatch: parsed {o:#x} of {len(d):#x}'
    return sec


def vga_palette(raw):
    return [min(255, (c & 0x3F) * 255 // 63) for c in raw[:768]]


def dump_fox_dat(path, out):
    d = open(path, 'rb').read()
    s = parse_fox_dat(d)
    info = {}
    for k in ('palette', 'ascii8x8', 'ascii8x16', 'cho', 'jung', 'jong'):
        info[k] = dict(offset=hex(s[k][0]), size=len(s[k][1]))
    info['records'] = [dict(offset=hex(r['offset']), n=r['n'], words=len(r['a']) // 2) for r in s['records']]
    pal = s['palette'][1]
    sw = [bytearray() for _ in range(16 * 8)]
    for i in range(256):
        for y in range(8):
            sw[(i // 16) * 8 + y] += bytes([i]) * 8
    write_png(os.path.join(out, 'fox_palette.png'), 128, 128, sw, vga_palette(pal))
    f8 = s['ascii8x8'][1]
    glyph_sheet(os.path.join(out, 'font_ascii_8x8.png'), [f8[i * 8:i * 8 + 8] for i in range(95)], 8, 8)
    f16 = s['ascii8x16'][1]
    glyph_sheet(os.path.join(out, 'font_ascii_8x16.png'), [f16[i * 16:i * 16 + 16] for i in range(95)], 8, 16)
    for name, (sets, n) in (('cho', (8, 19)), ('jung', (2, 21)), ('jong', (3, 27))):
        raw = s[name][1]
        gl = [[(raw[k * 32 + 2 * r] << 8) | raw[k * 32 + 2 * r + 1] for r in range(16)] for k in range(sets * n)]
        glyph_sheet(os.path.join(out, f'font_hangul_{name}.png'), gl, 16, 16, cols=n)
    json.dump(info, open(os.path.join(out, 'fox_dat.json'), 'w'), indent=1)
    print('FOX.DAT:', json.dumps(info))
    return vga_palette(pal)


# ------------------------------------------------------------ VSPRITE.PAT
def rle_decode(b, n):
    out, i = bytearray(), 0
    while i < len(b):
        c = b[i]; i += 1
        if c & 0x80:
            out += bytes([b[i]]) * (c & 0x7F); i += 1
        else:
            out += b[i:i + c]; i += c
    return bytes(out[:n])       # encoder pads trailing runs by a few bytes


def parse_vsprite(d, count=208):
    dir_ = d[-count * 6:]
    entries, o = [], 0
    for k in range(count):
        w, h, sz = struct.unpack_from('<HHH', dir_, 6 * k)
        n = sz if sz else w * h          # size 0 => stored raw
        entries.append(dict(index=k, w=w, h=h, packed=sz, offset=o))
        o += n
    assert o == len(d) - count * 6, 'VSPRITE.PAT directory does not cover data'
    return entries


def dump_vsprite(path, out, palette):
    d = open(path, 'rb').read()
    ents = parse_vsprite(d)
    sd = os.path.join(out, 'sprites'); os.makedirs(sd, exist_ok=True)
    for e in ents:
        raw = d[e['offset']:e['offset'] + (e['packed'] or e['w'] * e['h'])]
        px = rle_decode(raw, e['w'] * e['h']) if e['packed'] else raw
        rows = [px[y * e['w']:(y + 1) * e['w']] for y in range(e['h'])]
        if e['w'] and e['h']:
            write_png(os.path.join(sd, f"{e['index']:03d}_{e['w']}x{e['h']}.png"), e['w'], e['h'], rows, palette)
    json.dump(ents, open(os.path.join(out, 'vsprite.json'), 'w'), indent=0)
    print(f'VSPRITE.PAT: {len(ents)} sprites -> {sd}')


# ----------------------------------------------------------- STAGE / VOC
def dump_stage(path, out):
    d = open(path, 'rb').read()
    for k in range(len(d) // 0x1000):
        open(os.path.join(out, f'stage{k + 1}.bin'), 'wb').write(d[k * 0x1000:(k + 1) * 0x1000])
    print(f'STAGE.DAT: {len(d) // 0x1000} x 4096-byte stage records (loaded via fseek(stage<<12))')


def dump_voc(path, out):
    """VOICE.DAT = 18 concatenated VOC files (lengths: u16 table at DS:0B3E in FOX.EXE;
    clips 0-7 loaded singly, 8-17 = resident SFX bank). Each clip -> WAV."""
    import wave
    d = open(path, 'rb').read()
    starts, i = [], 0
    while True:
        i = d.find(b'Creative Voice File\x1a', i)
        if i < 0:
            break
        starts.append(i); i += 1
    vd = os.path.join(out, 'voice'); os.makedirs(vd, exist_ok=True)
    clips = []
    for n, s0 in enumerate(starts):
        e0 = starts[n + 1] if n + 1 < len(starts) else len(d)
        o = s0 + struct.unpack_from('<H', d, s0 + 0x14)[0]
        pcm, rate = bytearray(), 8000
        while o < e0 and d[o] != 0:
            t = d[o]; ln = d[o + 1] | d[o + 2] << 8 | d[o + 3] << 16
            if t == 1:
                rate = 1000000 // (256 - d[o + 4])
                if d[o + 5] == 0: pcm += d[o + 6:o + 4 + ln]
            elif t == 2:
                pcm += d[o + 4:o + 4 + ln]
            o += 4 + ln
        w = wave.open(os.path.join(vd, f'clip{n:02d}.wav'), 'wb')
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(rate); w.writeframes(bytes(pcm)); w.close()
        clips.append(dict(index=n, offset=s0, bytes=e0 - s0, rate=rate, samples=len(pcm)))
    json.dump(clips, open(os.path.join(out, 'voice_clips.json'), 'w'), indent=0)
    print(f'VOICE.DAT: {len(clips)} VOC clips (0-7 single, 8-17 resident bank) -> {vd}')


# ------------------------------------------------------------ EXE script
def johab_ok(s):
    i = n = 0
    while i < len(s):
        if s[i] & 0x80:
            if i + 1 >= len(s):
                return 0
            w = s[i] << 8 | s[i + 1]
            if not (2 <= (w >> 10 & 31) <= 20):
                return 0
            n += 1; i += 2
        else:
            i += 1
    return n


def text_ok(b):
    try:
        t = b.decode('johab')
    except UnicodeDecodeError:
        return False
    return all('\uac00' <= c <= '\ud7a3' or ' ' <= c <= '~' for c in t)


def dump_script(exe, out):
    f = open(exe, 'rb').read()
    hdr = struct.unpack_from('<H', f, 8)[0] * 16
    # DGROUP = MSC startup `mov di, DGROUP` at entry+0x0D (fallback: Fox Ranger value)
    ip, cs = struct.unpack_from('<HH', f, 0x14)
    ent = hdr + cs * 16 + ip
    dgroup = struct.unpack_from('<H', f, ent + 0x0E)[0] if f[ent + 0x0D] == 0xBF else 0x1ADF
    dbase = hdr + dgroup * 16
    rows, i = [], dbase
    while i < len(f):
        j = f.find(b'\0', i)
        if j < 0:
            break
        s = f[i:j]
        # strings may directly follow binary tables (no NUL separator):
        # take the longest suffix that is strictly valid Johab/ASCII text
        for p in range(0, max(0, len(s) - 5)):
            t = s[p:]
            if johab_ok(t) >= 3 and text_ok(t):
                rows.append(dict(file_offset=hex(i + p), ds_offset=hex(i + p - dbase), max_bytes=len(t),
                                 korean=t.decode('johab'), english=''))
                break
        i = j + 1
    p = os.path.join(out, 'script_johab.json')
    json.dump(rows, open(p, 'w'), ensure_ascii=False, indent=1)
    print(f'script: {len(rows)} Johab strings in DGROUP {dgroup:04X} -> {p}')


def extract_fr2_cutscenes(work_dir, out_json_path):
    """Extract far pointer tables and Johab narrative dialogue for Fox Ranger II."""
    modules = [
        ('PRO1', 'PRO1_UNP.EXE'),
        ('PRO2', 'PRO2_UNP.EXE'),
        ('PRO3', 'PRO3_UNP.EXE'),
        ('PRO4', 'PRO4_UNP.EXE'),
        ('VIS1', 'VIS1_UNP2.EXE'),
        ('VIS2', 'VIS2_UNP2.EXE'),
        ('VIS4', 'VIS4_UNP2.EXE'),
        ('VIS5', 'VIS5_UNP2.EXE'),
        ('EPIL', 'EPIL_UNP2.EXE'),
    ]

    def text_valid(b):
        try:
            t = b.decode('johab')
        except UnicodeDecodeError:
            return False, ''
        hangul_cnt = sum(1 for c in t if '\uac00' <= c <= '\ud7a3')
        ascii_cnt = sum(1 for c in t if ' ' <= c <= '~')
        if hangul_cnt >= 2 and (hangul_cnt + ascii_cnt) == len(t):
            return True, t
        return False, ''

    all_cutscenes = []
    total_strings = 0

    for mod_name, exe_name in modules:
        exe_path = os.path.join(work_dir, exe_name)
        if not os.path.exists(exe_path):
            alt_path = os.path.join(work_dir, exe_name.replace('_UNP2.EXE', '_UNP.EXE'))
            if os.path.exists(alt_path):
                exe_path = alt_path
            else:
                print(f"Skipping {mod_name}: {exe_path} not found")
                continue

        d = open(exe_path, 'rb').read()
        hdr = struct.unpack_from('<H', d, 8)[0] * 16
        img = d[hdr:]
        nrel = struct.unpack_from('<H', d, 6)[0]
        relofs = struct.unpack_from('<H', d, 0x18)[0]
        relocs = [
            struct.unpack_from('<HH', d, relofs + i * 4)[1] * 16 +
            struct.unpack_from('<HH', d, relofs + i * 4)[0]
            for i in range(nrel)
        ]

        entries = []
        seen_targets = set()
        for r in sorted(relocs):
            p = r - 2
            if 0 <= p < len(img) - 4:
                off, seg = struct.unpack_from('<HH', img, p)
                target = seg * 16 + off
                if 0 <= target < len(img) - 2:
                    end = img.find(b'\0', target)
                    if end > target + 2:
                        ok, txt = text_valid(img[target:end])
                        if ok and target not in seen_targets:
                            seen_targets.add(target)
                            idx = len(entries) + 1
                            item_id = f"{mod_name}_{idx:03d}"
                            if mod_name == 'EPIL' and idx > 26 and idx <= 46:
                                item_id = f"EPIL_CREDIT_{idx-26:02d}"
                            elif mod_name == 'EPIL' and idx > 46:
                                item_id = f"EPIL_DEDICATION_{idx-46:02d}"

                            entries.append({
                                'id': item_id,
                                'module': mod_name,
                                'ptr_img': f"0x{p:04x}",
                                'ptr_file': f"0x{hdr + p:06x}",
                                'seg': f"0x{seg:04x}",
                                'off': f"0x{off:04x}",
                                'target_img': f"0x{target:04x}",
                                'target_file': f"0x{hdr + target:06x}",
                                'max_bytes': len(img[target:end]),
                                'korean': txt,
                                'english': ""
                            })

        print(f"  {mod_name} ({os.path.basename(exe_path)}): extracted {len(entries)} dialogue/narrative strings")
        all_cutscenes.extend(entries)
        total_strings += len(entries)

    os.makedirs(os.path.dirname(os.path.abspath(out_json_path)), exist_ok=True)
    with open(out_json_path, 'w', encoding='utf-8') as f:
        json.dump(all_cutscenes, f, ensure_ascii=False, indent=2)

    print(f"Extracted total of {total_strings} strings -> {out_json_path}")
    return all_cutscenes


def main():
    if '--cutscenes' in sys.argv:
        c_idx = sys.argv.index('--cutscenes')
        work_dir = sys.argv[c_idx + 1] if len(sys.argv) > c_idx + 1 and not sys.argv[c_idx + 1].startswith('--') else 'work'
        out_path = sys.argv[c_idx + 2] if len(sys.argv) > c_idx + 2 and not sys.argv[c_idx + 2].startswith('--') else 'work/cutscenes_kr_extracted.json'   # work/ is git-ignored: the Korean script stays local
        extract_fr2_cutscenes(work_dir, out_path)
        return

    if len(sys.argv) < 3:
        sys.exit(__doc__ + "\n\nOr run: dump_assets.py --cutscenes [WORK_DIR] [OUT_JSON]")
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pal = dump_fox_dat(os.path.join(game, 'FOX.DAT'), out)
    dump_vsprite(os.path.join(game, 'VSPRITE.PAT'), out, pal)
    dump_stage(os.path.join(game, 'STAGE.DAT'), out)
    dump_voc(os.path.join(game, 'VOICE.DAT'), out)
    if '--exe' in sys.argv:
        dump_script(sys.argv[sys.argv.index('--exe') + 1], out)



if __name__ == '__main__':
    main()
