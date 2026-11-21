#!/usr/bin/env python3
"""batch_stage_recomp.py - Batch transpilation of all Fox Ranger II stages.

Iterates over ST2, ST22, ST3, ST32, ST4, ST42, ST5, ST6, ST62, ST63, FOX2B:
1. Filters eligible functions from triage CSV (excluding CRT segment and disassembler overflows).
2. Generates <stage>_modules.csv.
3. Invokes recomp.py with --prefix <stage>.
"""
import csv, os, subprocess, sys

STAGES = [
    ('st22',  'work/ST22_UNP2.EXE', '0f30'),
    ('st3',   'work/ST3_UNP2.EXE',  '0e85'),
    ('st32',  'work/ST32_UNP2.EXE', '0fbf'),
    ('st4',   'work/ST4_UNP2.EXE',  '0f2b'),
    ('st42',  'work/ST42_UNP2.EXE', '0ea7'),
    ('st5',   'work/ST5_UNP2.EXE',  '0913'),
    ('st6',   'work/ST6_UNP2.EXE',  '0d34'),
    ('st62',  'work/ST62_UNP2.EXE', '0da3'),
    ('st63',  'work/ST63_UNP2.EXE', '0d8c'),
    ('fox2b', 'work/FOX2B_UNP.EXE', '061a'),
]

def main():
    print("=== Batch Recompilation for Fox Ranger II Stages ===")
    for stage_name, exe_path, crt_seg in STAGES:
        funcs_csv = f'work/{stage_name}_funcs.csv'
        if not os.path.exists(funcs_csv):
            print(f"[{stage_name}] Generating funcs.csv...")
            subprocess.run(['python3', 'tools/triage_recomp.py', '--csv', funcs_csv, exe_path], check=True, stdout=subprocess.DEVNULL)

        modules_csv = f'work/{stage_name}_modules.csv'
        eligible = []
        with open(funcs_csv) as f:
            for row in csv.DictReader(f):
                seg = row['seg'].lower()
                size = int(row['size']) if row['size'] else 0
                if size > 60000 or seg == crt_seg.lower():
                    continue
                eligible.append((row['linear'], row['seg'], row['off'], 'game_logic', 'transpile', f'{stage_name}_fn_{row["seg"]}_{row["off"]}'))

        with open(modules_csv, 'w') as f:
            f.write('linear,seg,off,module,strategy,name\n')
            for r in eligible:
                f.write(','.join(r) + '\n')

        print(f"[{stage_name}] Transpiling {len(eligible)} functions with recomp.py...")
        cmd = [
            'python3', 'tools/recomp.py', exe_path, modules_csv, 'gen/',
            '--modules', 'game_logic', '--prefix', stage_name
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        print(f"  {res.stdout.strip()}")
        if res.returncode != 0:
            print(f"  STDERR: {res.stderr.strip()}")

    print("=== Batch Transpilation Complete ===")

if __name__ == '__main__':
    main()
