#!/usr/bin/env python3
"""regen_fr2.py - regenerate the recompiled code of every Fox Ranger II program.

Adds entry points the triage pass cannot see because nothing calls them
directly:
  * the music-timer library's INT 8 handler, installed with
    `mov dx, imm / mov bx, cs / mov ds, bx / mov ax, 2508h / int 21h`
    (found by scanning the generated code of the installer);
  * FOX2B's assembly helpers linked into the crt0 segment (061A:0000-022B).
"""
import csv, json, os, re, glob, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import crt_ident

CSV = {'st1': 'work/st1_recomp_modules.csv'}


def isr_entries(mod):
    found = set()
    for f in glob.glob(os.path.join(ROOT, 'gen', f'recomp_{mod}_*.c')):
        L = open(f).read().split('\n'); seg = None
        for i, l in enumerate(L):
            mm = re.match(r'void f_\w+_([0-9A-F]{4})_([0-9A-F]{4})\(Cpu \*c\)$', l)
            if mm:
                seg = mm.group(1)
            if 'rt_interrupt(c, 0x21)' in l:
                ctx = [re.sub(r'.*/\* [0-9A-F]+: (.*?) \*/.*', r'\1', x).strip()
                       for x in L[max(0, i - 40):i] if '/* ' in x][-8:]
                s = ' | '.join(ctx)
                md = re.search(r'mov dx, (0x[0-9a-f]+) \| mov bx, cs \| mov ds, bx', s)
                if md and 'mov ah, 0x25' in s:
                    found.add(f'{seg}:{int(md.group(1), 16):04X}')
    return found


def cs_handler_entries(mod):
    """`mov dx, cs / mov ax, imm` (vector-install helpers, e.g. the SB
    library's timer calibration ISR) whose target ends in iret."""
    exe = crt_ident.Exe(crt_ident.exe_for(mod))
    found = set()
    for f in glob.glob(os.path.join(ROOT, 'gen', f'recomp_{mod}_*.c')):
        L = open(f).read().split('\n'); seg = None
        for i, l in enumerate(L):
            mm = re.match(r'void f_\w+_([0-9A-F]{4})_([0-9A-F]{4})\(Cpu \*c\)$', l)
            if mm:
                seg = int(mm.group(1), 16)
            if '/* ' in l and ': mov dx, cs */' in l:
                for x in L[i + 1:i + 6]:
                    m2 = re.search(r'/\* [0-9A-F]+: mov ax, (0x[0-9a-f]+) \*/', x)
                    if m2:
                        off = int(m2.group(1), 16)
                        base = seg * 16
                        for k, ins in enumerate(crt_ident.md.disasm(exe.img[base + off:base + off + 96], off)):
                            if ins.mnemonic == 'iret':
                                found.add(f'{seg:04X}:{off:04X}'); break
                            if ins.mnemonic in ('ret', 'retf') or k > 24:
                                break
                        break
    return found


def sb_irq_probe_entries(mod, lib_segs):
    """SB library IRQ-probe handlers: `push ds/ax/dx; mov ax, DGROUP;
    mov ds, ax; mov ax, IRQ; jmp tail` (one per candidate IRQ), located in
    the same segment as the library's timer-calibration ISR."""
    exe = crt_ident.Exe(crt_ident.exe_for(mod))
    found = set()
    for seg in lib_segs:
        base = seg * 16
        blob = exe.img[base:base + 0x1000]
        for m in re.finditer(rb'\x1e\x50\x52\xb8..\x8e\xd8\xb8[\x02\x03\x05\x07\x0a]\x00', blob, re.S):
            found.add(f'{seg:04X}:{m.start():04X}')
    return found


def subtitle_nops(mod):
    """PRO3/PRO4 (and PRO1) skip the dialogue text while the Sound Blaster
    speech plays:  mov al, es:[604h] / and ax, 0FFh / cmp ax, 19h /
    jne print / jmp past_print.  Patching the `jmp` out keeps the subtitles
    on screen together with the voice (the voice calls are separate `je`
    branches and stay untouched)."""
    exe = crt_ident.Exe(crt_ident.exe_for(mod))
    starts = sorted((int(s_, 16) * 16 + int(o, 16), int(s_, 16)) for s_, o in
                    re.findall(r'void f_\w+?_([0-9A-F]{4})_([0-9A-F]{4})\(Cpu', open(os.path.join(ROOT, 'gen', f'recomp_{mod}_gen.h')).read()))
    out = set()
    for m in re.finditer(rb'\x26\xa0..\x25\xff\x00\x3d\x19\x00\x75\x03\xe9', exe.img, re.S):
        lin = m.end() - 1                                  # the jmp
        seg = max((st for st in starts if st[0] <= lin), default=(0, 0))[1]
        out.add(f'{seg:04X}:{lin - seg * 16:04X}')
    return out


def fox2b_helpers():
    out = set()
    for r in csv.DictReader(open(os.path.join(ROOT, 'work/fox2b_funcs.csv'))):
        if r['seg'].upper() == '061A' and int(r['off'], 16) < 0x022C:
            out.add(f"061A:{int(r['off'], 16):04X}")
    return out


def main():
    mods = sys.argv[1:] or crt_ident.MODULES
    for m in mods:
        cs_h = cs_handler_entries(m)
        extra = isr_entries(m) | cs_h | sb_irq_probe_entries(m, {int(e.split(':')[0], 16) for e in cs_h})
        if m == 'fox2b':
            extra |= fox2b_helpers()
        exe = crt_ident.exe_for(m)
        csvp = os.path.join(ROOT, CSV.get(m, f'work/{m}_modules.csv'))
        cmd = ['python3', os.path.join(ROOT, 'tools/recomp.py'), exe, csvp, os.path.join(ROOT, 'gen'),
               '--modules', 'game_logic', '--prefix', m, '--extra-module', 'game_logic',
               '--extra', ','.join(sorted(extra)), '--nop', ','.join(sorted(subtitle_nops(m)))]
        r = subprocess.run(cmd, capture_output=True, text=True)
        print(f'{m:6s} +{len(extra):2d} extra: {r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""}')
        if r.returncode:
            print(r.stderr); return 1
        for l in open(os.path.join(ROOT, 'gen/recomp_report.txt')):
            if l.startswith('FAILED'):
                print('     ', l.strip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
