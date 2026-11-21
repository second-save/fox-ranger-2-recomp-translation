#!/usr/bin/env python3
"""analyze_crt.py - Find CRT function offsets in unpacked DOS MZ executables.

Looks for common MSC 6.0 CRT function signatures in the CRT segment.
"""
import struct
import sys
import os

# Common CRT function signatures (first few bytes of prologue/code)
# These are patterns that appear in MSC 6.0 large model CRT
SIGNATURES = {
    # exit: typically starts with xor ax,ax or mov ah,4ch
    'exit': [
        bytes([0xB8, 0x00, 0x4C]),  # mov ax, 4C00h
        bytes([0xB4, 0x4C]),        # mov ah, 4Ch
    ],
    # fopen: push bp; mov bp,sp; sub sp,...
    'fopen': [
        bytes([0x55, 0x8B, 0xEC, 0x83, 0xEC]),
    ],
    # fclose
    'fclose': [
        bytes([0x55, 0x8B, 0xEC, 0x56]),  # push bp; mov bp,sp; push si
    ],
    # fread: characteristic pattern
    'fread': [
        bytes([0x55, 0x8B, 0xEC, 0x83, 0xEC]),
    ],
    # sprintf: usually has format string handling
    'sprintf': [
        bytes([0x55, 0x8B, 0xEC]),
    ],
}

def read_exe(path):
    """Read MZ executable and return (image_data, header_info)"""
    with open(path, 'rb') as f:
        data = f.read()
    
    if len(data) < 0x1C or data[0:2] != b'MZ':
        raise ValueError(f"Not a valid MZ executable: {path}")
    
    hdr_size = struct.unpack_from('<H', data, 8)[0] * 16
    cs = struct.unpack_from('<H', data, 0x16)[0]
    ip = struct.unpack_from('<H', data, 0x14)[0]
    ss = struct.unpack_from('<H', data, 0x0E)[0]
    
    image = data[hdr_size:]
    return image, {'cs': cs, 'ip': ip, 'ss': ss, 'hdr_size': hdr_size}

def find_string(image, s):
    """Find a string in the image and return offsets"""
    encoded = s.encode('ascii') + b'\x00'
    offsets = []
    pos = 0
    while True:
        pos = image.find(encoded, pos)
        if pos == -1:
            break
        offsets.append(pos)
        pos += 1
    return offsets

def find_crt_functions(image, crt_seg_offset):
    """Try to find common CRT functions in the CRT segment area"""
    results = {}
    
    # The CRT segment starts at crt_seg_offset * 16 in the image
    # But the segment value is relative to load address, so we need to 
    # look at the actual code
    
    # Common CRT function name strings that might appear
    crt_strings = [
        'fopen', 'fclose', 'fread', 'fwrite', 'fseek', 'ftell',
        'printf', 'sprintf', 'fprintf',
        'malloc', 'free', 'calloc', 'realloc',
        'exit', '_exit',
        'memcpy', 'memset', 'memmove', 'strlen', 'strcpy', 'strcmp',
        'rand', 'srand',
        'inp', 'outp', 'inpw', 'outpw',
    ]
    
    for name in crt_strings:
        offsets = find_string(image, name)
        if offsets:
            results[name + '_str'] = offsets
    
    return results

def analyze_int21_calls(image):
    """Find INT 21h calls and categorize them"""
    calls = []
    # INT 21h is CD 21
    pos = 0
    while True:
        pos = image.find(b'\xCD\x21', pos)
        if pos == -1:
            break
        # Look back for MOV AH, xx pattern
        if pos >= 2:
            if image[pos-2] == 0xB4:  # mov ah, imm8
                ah_val = image[pos-1]
                calls.append((pos, ah_val))
        pos += 1
    return calls

def find_prologue_pattern(image, start_offset, end_offset):
    """Find function prologues (push bp; mov bp,sp) in a range"""
    pattern = bytes([0x55, 0x8B, 0xEC])  # push bp; mov bp, sp
    funcs = []
    pos = start_offset
    while pos < end_offset:
        pos = image.find(pattern, pos, end_offset)
        if pos == -1:
            break
        funcs.append(pos)
        pos += 1
    return funcs

def main():
    if len(sys.argv) < 2:
        print("Usage: analyze_crt.py <exe_file> [exe_file...]")
        sys.exit(1)
    
    for path in sys.argv[1:]:
        if not os.path.exists(path):
            print(f"File not found: {path}")
            continue
        
        print(f"\n{'='*60}")
        print(f"Analyzing: {path}")
        print(f"{'='*60}")
        
        try:
            image, info = read_exe(path)
        except Exception as e:
            print(f"Error reading {path}: {e}")
            continue
        
        print(f"Entry CS:IP = {info['cs']:04X}:{info['ip']:04X}")
        print(f"Stack SS = {info['ss']:04X} (DGROUP)")
        print(f"CRT segment = 0x{info['cs']:04X}")
        print(f"Image size = {len(image)} bytes")
        
        # Find INT 21h calls grouped by function
        int21_calls = analyze_int21_calls(image)
        ah_counts = {}
        for pos, ah in int21_calls:
            ah_counts[ah] = ah_counts.get(ah, 0) + 1
        
        print(f"\nINT 21h calls by function:")
        for ah in sorted(ah_counts.keys()):
            desc = {
                0x3C: "create file",
                0x3D: "open file", 
                0x3E: "close file",
                0x3F: "read file",
                0x40: "write file",
                0x42: "lseek",
                0x48: "alloc memory",
                0x49: "free memory",
                0x4C: "exit",
                0x25: "set int vector",
                0x35: "get int vector",
            }.get(ah, "")
            print(f"  AH={ah:02X}: {ah_counts[ah]:3d} calls  {desc}")
        
        # Find exit function (look for INT 21h with AH=4Ch)
        exit_calls = [(pos, ah) for pos, ah in int21_calls if ah == 0x4C]
        if exit_calls:
            print(f"\nexit() candidates (INT 21h/4Ch):")
            for pos, _ in exit_calls[:5]:
                # Find function start (prologue before this)
                for back in range(min(100, pos), 0, -1):
                    if image[pos-back:pos-back+3] == bytes([0x55, 0x8B, 0xEC]):
                        func_start = pos - back
                        seg = func_start // 16
                        off = func_start % 16
                        print(f"  image offset 0x{func_start:05X} (possible seg:off {seg:04X}:{off:04X})")
                        break
        
        # Find file operation functions
        file_funcs = {0x3D: 'fopen', 0x3E: 'fclose', 0x3F: 'fread', 0x40: 'fwrite', 0x42: 'fseek'}
        for ah, name in file_funcs.items():
            calls = [(pos, ah_) for pos, ah_ in int21_calls if ah_ == ah]
            if calls:
                print(f"\n{name}() candidates (INT 21h/{ah:02X}h):")
                for pos, _ in calls[:3]:
                    for back in range(min(200, pos), 0, -1):
                        if image[pos-back:pos-back+3] == bytes([0x55, 0x8B, 0xEC]):
                            func_start = pos - back
                            crt_base = info['cs'] * 16
                            if func_start >= crt_base:
                                off = func_start - crt_base
                                print(f"  CRT offset 0x{off:04X} (image 0x{func_start:05X})")
                            break

if __name__ == '__main__':
    main()
