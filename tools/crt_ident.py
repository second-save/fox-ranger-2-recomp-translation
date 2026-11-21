#!/usr/bin/env python3
"""crt_ident.py - identify MSC 6 C runtime functions in every FR2 module.

Reference: Fox Ranger 1 (FOX_UNP.EXE, CRT segment 134D) whose CRT table was
validated by the working FR1 port.  For each FR2 module we collect every CRT
offset the generated code calls (rt_call_far(c, NULL, CRTSEG, off, ...)),
follow `jmp far` thunks to the body, and compare the body with every
reference function at the instruction level: mnemonics, registers and small
immediates must match; absolute addresses, relocated words and branch targets
are wildcarded (they legitimately differ between links).

Also extracts the MSC startup facts each module needs at run time (DGROUP,
initial SP adjustment, BSS range, main()).

Writes src/fr2_modules_gen.c.  Unidentified CRT calls are reported and make
the tool exit non-zero unless listed in MANUAL below (verified by hand).
"""
import glob, os, re, struct, sys
import capstone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF_EXE = os.path.join(ROOT, '..', 'fox-ranger', 'work', 'FOX_UNP.EXE')
REF_SEG = 0x134D
REF_TABLE = {  # validated FR1 table (crt_shims.c: crt_register_segment)
    0x01DF: 'exit', 0x06A6: 'fclose', 0x07A0: 'fopen', 0x07BC: 'fread',
    0x092E: 'fwrite', 0x0AC6: 'puts', 0x19CC: '_fmalloc', 0x19D2: '_ffree',
    0x1C92: '_fmsize', 0x1CE6: '_fstrcpy', 0x1D1C: '_fstrlen', 0x1D7C: 'fseek',
    0x1F98: 'sprintf', 0x2004: 'inp', 0x2012: 'outp', 0x2020: '_fmemmove',
    0x2148: '_fmemset', 0x21F4: 'abs', 0x2214: 'rand', 0x224C: '_dos_allocmem',
    0x2266: '_chain_intr', 0x2282: '_dos_getvect', 0x229F: '_dos_findfirst',
    0x22D2: '_dos_setvect', 0x22E8: '_disable', 0x22EA: '_enable',
    0x22EC: '_aFldiv', 0x2386: '_aFlmul', 0x23B8: '_aFlrem', 0x2458: '_aFlshl',
    0x2464: '_aFlshr', 0x2470: '_fmemcpy',
}
# (module, crt offset) -> name, for functions FR1 never linked.  Each entry
# was checked by reading the disassembly (see --dump).
MANUAL = {}
# Hand-verified exemplars for runtime functions FR1 never linked:
# (module, crt offset) -> name.  Every body with the same depth-0 signature in
# any module gets the same name.  Verified by reading the disassembly:
EXEMPLARS = {
    ('st1', 0x09E8): 'execl',        # push environ; lea args; push path; call _execve
    ('st2', 0x2058): 'rewind',       # flush, clear EOF/ERR + _osfile EOF, lseek(fd,0,0)
    ('st2', 0x3042): '_aFulrem',     # unsigned 32-bit remainder (dx:ax)
    ('pwd', 0x3074): '_dos_open',    # int 21h/3Dh, handle -> *[bp+0Ch]
    ('fox2b', 0x156E): '_dos_close', # int 21h/3Eh, clears _osfile[fd]
    ('pro1', 0x05DA): '_dos_close',
    ('fox2b', 0x1622): 'open',       # 3Dh, falls back to create when O_CREAT
    ('pro1', 0x0614): 'open',
    ('fox2b', 0x17D4): 'read',       # 3Fh with text-mode CR/LF + ^Z handling
    ('pro1', 0x07C6): 'read',
    ('start', 0x1AC0): 'atol',       # skips blanks, sign, decimal -> dx:ax
    ('pro1', 0x0C04): '_dos_freemem',# int 21h/49h
    ('pwd', 0x1FCE): 'time',         # 2Ah/2Ch/2Ah date-time -> seconds
    ('pwd', 0x255A): 'srand',        # seed -> rand state
    ('score', 0x1C50): 'itoa',       # (int, char far *, radix)
    ('score', 0x1C6C): 'ltoa',       # (long, char far *, radix)
    ('score', 0x1D08): 'getch',      # 21h/08h, ungetch buffer
    ('fox2b', 0x5200): '_nop_hook',  # only reached when [10FE]==D6D6 (never set)
}
# FOX2B links its own assembly helpers into the entry segment below crt0;
# those are game code and are recompiled, not shimmed.
GAME_CODE_IN_CRT = {'fox2b': (0x0000, 0x022C)}

MODULES = ['st1', 'st2', 'st22', 'st3', 'st32', 'st4', 'st42', 'st5', 'st6',
           'st62', 'st63', 'fox2b', 'tit', 'start', 'pwd', 'score', 'so', 'ove',
           'pro1', 'pro2', 'pro3', 'pro4', 'vis1', 'vis2', 'vis4', 'vis5', 'epil']

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
md.detail = False


class Exe:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.path = path
        hdr = struct.unpack_from('<H', d, 8)[0] * 16
        nrel, = struct.unpack_from('<H', d, 6)
        relofs, = struct.unpack_from('<H', d, 0x18)
        self.ss, self.sp, _, self.ip, self.cs = struct.unpack_from('<HHHHH', d, 0x0E)
        last, pages = struct.unpack_from('<HH', d, 2)
        size = pages * 512 - (512 - last if last else 0)
        self.img = d[hdr:size]
        self.rel = set()
        for i in range(nrel):
            o, s = struct.unpack_from('<HH', d, relofs + 4 * i)
            a = s * 16 + o
            self.rel.update((a, a + 1))

    def w(self, a):
        return struct.unpack_from('<H', self.img, a)[0]


NUM = re.compile(r'0x[0-9a-f]+|\b\d+\b')


def norm_ins(exe, ins, base):
    m = ins.mnemonic
    ops = ins.op_str
    a = base + ins.address
    if any(x in exe.rel for x in range(a, a + ins.size)):
        ops = NUM.sub('#', ops)
    elif m.startswith('j') or m in ('call', 'lcall', 'loop', 'loope', 'loopne', 'jcxz'):
        ops = NUM.sub('#', ops)
    else:
        ops = NUM.sub(lambda t: t.group(0) if int(t.group(0), 0) < 0x100 else '#', ops)
    return f'{m} {ops}'


def body(exe, seg, off, limit=160, depth=2):
    """normalized instruction list from seg:off to the first return (inclusive).
    Direct call targets are replaced by the (shallower) signature of the callee
    so thin wrappers such as fread/fwrite stay distinguishable."""
    base = seg * 16
    out = []
    for ins in md.disasm(exe.img[base + off: base + off + limit * 6], off):
        t = norm_ins(exe, ins, base)
        if depth > 0 and ins.mnemonic in ('call', 'lcall'):
            try:
                if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
                    t += ' {' + ';'.join(body(exe, seg, int(ins.op_str, 16), 60, depth - 1)) + '}'
                elif ins.mnemonic == 'lcall' and ',' in ins.op_str and 'ptr' not in ins.op_str:
                    cs_, co = (int(x, 16) for x in ins.op_str.split(','))
                    cs_, co = resolve_thunk(exe, cs_, co)
                    t += ' {' + ';'.join(body(exe, cs_, co, 60, depth - 1)) + '}'
            except (ValueError, IndexError, struct.error):
                pass
        out.append(t)
        if ins.mnemonic in ('ret', 'retf', 'iret') or len(out) >= limit:
            break
        if ins.mnemonic == 'ljmp' or (ins.mnemonic == 'jmp' and ins.op_str.startswith('word ptr')):
            break
    return out


def resolve_thunk(exe, seg, off):
    """follow `jmp far seg:off` thunks (EA oo oo ss ss) inside the image."""
    for _ in range(4):
        a = seg * 16 + off
        if exe.img[a] == 0xEA:
            off, seg = exe.w(a + 1), exe.w(a + 3)
            continue
        break
    return seg, off


def startup(exe):
    """parse the MSC 6 crt0 startup code at the entry point."""
    e = exe.cs * 16 + exe.ip
    code = exe.img[e:e + 0x200]
    info = {}
    i = code.find(b'\xbf')                         # mov di, DGROUP
    info['dgroup'] = struct.unpack_from('<H', code, i + 1)[0]
    i = code.find(b'\x81\xc4')                     # add sp, imm
    info['sp_add'] = struct.unpack_from('<H', code, i + 2)[0]
    i = code.find(b'\xf3\xaa')                     # rep stosb (BSS clear)
    pre = code[:i]
    j = pre.rfind(b'\xbf')                         # mov di, _edata
    k = pre.rfind(b'\xb9')                         # mov cx, _end
    info['bss_lo'] = struct.unpack_from('<H', pre, j + 1)[0]
    info['bss_hi'] = struct.unpack_from('<H', pre, k + 1)[0]
    # 5 x `push word ptr [x]` (FF 36 xx xx) followed by `lcall main` (9A oo oo ss ss)
    m = re.search(rb'(\xff\x36..){5}\x9a(....)', code, re.S)
    off, seg = struct.unpack('<HH', m.group(2))
    argc_addr = struct.unpack_from('<H', code, m.start() + 4 * 4 + 2)[0]  # last push = argc
    info['main'] = (seg, off)
    info['argc_var'] = argc_addr
    return info


def called_crt(mod, crt):
    offs = set()
    pat = re.compile(rf'rt_call_far\(c, NULL, 0x{crt:04X}, 0x([0-9A-F]+)')
    for f in glob.glob(os.path.join(ROOT, 'gen', f'recomp_{mod}_*.c')):
        for line in open(f):
            for mm in pat.finditer(line):
                offs.add(int(mm.group(1), 16))
    return sorted(offs)


def other_external(mod, crt):
    """non-CRT calls the recompiler left unresolved (fn == NULL)."""
    res = set()
    pat = re.compile(r'rt_call_far\(c, NULL, 0x([0-9A-F]+), 0x([0-9A-F]+)')
    for f in glob.glob(os.path.join(ROOT, 'gen', f'recomp_{mod}_*.c')):
        for line in open(f):
            for mm in pat.finditer(line):
                s = int(mm.group(1), 16)
                if s != crt:
                    res.add((s, int(mm.group(2), 16)))
    return sorted(res)


def exe_for(mod):
    for p in (f'work/{mod.upper()}_UNP2.EXE', f'work/{mod.upper()}_UNP.EXE'):
        p = os.path.join(ROOT, p)
        if os.path.exists(p):
            return p
    raise SystemExit(f'no exe for {mod}')


def main():
    dump = '--dump' in sys.argv
    ref = Exe(REF_EXE)
    refsig, refdeep = {}, {}
    for off, name in REF_TABLE.items():
        s, o = resolve_thunk(ref, REF_SEG, off)
        refsig.setdefault(tuple(body(ref, s, o, depth=0)), set()).add(name)
        refdeep[name] = [tuple(body(ref, s, o, depth=d)) for d in (1, 2)]

    for (m, off), name in EXEMPLARS.items():
        exe = Exe(exe_for(m)); bs, bo = resolve_thunk(exe, exe.cs, off)
        refsig.setdefault(tuple(body(exe, bs, bo, depth=0)), set()).add(name)
    rows, problems = [], []
    for mod in MODULES:
        exe = Exe(exe_for(mod))
        st = startup(exe)
        crt = exe.cs
        ents = []
        for off in called_crt(mod, crt):
            lo_hi = GAME_CODE_IN_CRT.get(mod)
            if lo_hi and lo_hi[0] <= off < lo_hi[1]:
                continue
            s, o = resolve_thunk(exe, crt, off)
            sig = tuple(body(exe, s, o, depth=0))
            names = refsig.get(sig)
            name = MANUAL.get((mod, off))
            if name is None and names and len(names) > 1:
                for d in (1, 2):           # tie-break with callee signatures
                    deep = tuple(body(exe, s, o, depth=d))
                    sub = {n for n in names if refdeep[n][d - 1] == deep}
                    if len(sub) == 1:
                        names = sub
                        break
            if name is None and names:
                if len(names) > 1:
                    problems.append(f'{mod} {crt:04X}:{off:04X} ambiguous {sorted(names)}')
                    continue
                name = next(iter(names))
            if name is None:
                problems.append(f'{mod} {crt:04X}:{off:04X} (body {s:04X}:{o:04X}) UNIDENTIFIED')
                if dump:
                    for l in body(exe, s, o, 40):
                        print('      ', l)
                continue
            ents.append((off, name))
        ext = other_external(mod, crt)
        rows.append((mod, exe, st, crt, ents, ext))
        print(f'{mod:6s} crt {crt:04X} dgroup {st["dgroup"]:04X} main {st["main"][0]:04X}:{st["main"][1]:04X} '
              f'bss {st["bss_lo"]:04X}-{st["bss_hi"]:04X} crt-calls {len(ents)} ext {[f"{a:04X}:{b:04X}" for a, b in ext]}')

    out = [
        '/* fr2_modules_gen.c - GENERATED by tools/crt_ident.py; do not edit. */',
        '#include "fr2_modules.h"', '',
    ]
    for mod, exe, st, crt, ents, _ in rows:
        out.append(f'static const Fr2CrtEntry crt_{mod}[] = {{')
        for off, name in ents:
            out.append(f'    {{ 0x{off:04X}, "{name}" }},')
        out.append('    { 0, 0 }')
        out.append('};')
    out.append('')
    out.append('const Fr2Module g_fr2_modules[] = {')
    for mod, exe, st, crt, ents, _ in rows:
        out.append(f'    {{ "{mod}", 0x{crt:04X}, 0x{st["dgroup"]:04X}, 0x{exe.ss:04X}, 0x{exe.sp:04X}, '
                   f'0x{st["sp_add"]:04X}, 0x{st["bss_lo"]:04X}, 0x{st["bss_hi"]:04X}, '
                   f'0x{st["main"][0]:04X}, 0x{st["main"][1]:04X}, crt_{mod} }},')
    out.append('    { 0 }')
    out.append('};')
    open(os.path.join(ROOT, 'src', 'fr2_modules_gen.c'), 'w').write('\n'.join(out) + '\n')

    if problems:
        print('\nPROBLEMS:')
        for p in problems:
            print('  ', p)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
