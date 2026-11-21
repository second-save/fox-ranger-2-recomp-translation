#!/usr/bin/env python3
"""map_crt.py - Map CRT function offsets between modules by comparing byte signatures.

Compares CRT code from a known module (ST1) against a target module to find
matching function offsets.
"""
import struct
import sys
import os
import re

# Known ST1 CRT mappings (offset -> name) at CRT segment 0x0DBD
ST1_CRT = {
    0x01DF: "exit",
    0x030A: "fclose",
    0x0404: "fopen",
    0x0420: "fread",
    0x0592: "fwrite",
    0x072A: "printf",
    0x076E: "_fmalloc",
    0x0774: "_ffree",
    0x077A: "_fstrlen",
    0x0792: "fseek",
    0x0814: "sprintf",
    0x0880: "inp",
    0x088E: "outp",
    0x089C: "_fmemcpy",
    0x0966: "_fmemset",
    0x09C0: "rand",
    0x09E8: "spawnl",
    0x0A08: "_dos_allocmem",
    0x0A3E: "_dos_getvect",
    0x0A50: "_dos_setvect",
    0x0A66: "_disable",
    0x0A68: "_enable",
    0x0A6A: "_aFldiv",
    0x0B04: "_aFlmul",
    0x0B36: "_aFlrem",
    0x0BD6: "_aFlshl",
    0x0BE2: "_aFlshr",
}

ST1_CRT_SEG = 0x0DBD

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

def get_signature(image, seg, offset, length=16):
    """Get bytes at segment:offset as signature"""
    addr = seg * 16 + offset
    if addr + length > len(image):
        return None
    return image[addr:addr+length]

def find_signature(image, sig, start_seg=0, end_seg=0x3000):
    """Find all occurrences of signature in image, return list of (seg, offset)"""
    matches = []
    for seg in range(start_seg, end_seg):
        for off in range(0, 0x1000, 2):  # Align to word boundaries
            addr = seg * 16 + off
            if addr + len(sig) > len(image):
                break
            if image[addr:addr+len(sig)] == sig:
                matches.append((seg, off))
    return matches

def map_crt_functions(ref_image, ref_crt_seg, target_image, target_crt_seg):
    """Map CRT functions from reference to target by signature matching"""
    results = {}
    
    for off, name in ST1_CRT.items():
        sig = get_signature(ref_image, ref_crt_seg, off, 12)
        if sig is None:
            print(f"  Warning: can't read {name} at {ref_crt_seg:04X}:{off:04X}")
            continue
        
        # Search in target CRT segment area
        target_addr = target_crt_seg * 16
        pos = target_addr
        while pos < len(target_image):
            pos = target_image.find(sig, pos)
            if pos == -1:
                break
            # Convert to segment:offset within CRT segment
            target_off = pos - target_addr
            if 0 <= target_off < 0x4000:  # CRT usually < 16KB
                results[target_off] = name
                break
            pos += 1
    
    return results

def extract_called_offsets(gen_dir, module_name, crt_seg):
    """Extract CRT offsets called from generated recomp code"""
    pattern = re.compile(rf'rt_call_far\(c, NULL, 0x{crt_seg:04X}, 0x([0-9A-Fa-f]+)')
    offsets = set()
    
    for fname in os.listdir(gen_dir):
        if fname.startswith(f'recomp_{module_name}_') and fname.endswith('.c'):
            path = os.path.join(gen_dir, fname)
            with open(path) as f:
                for line in f:
                    m = pattern.search(line)
                    if m:
                        offsets.add(int(m.group(1), 16))
    
    return sorted(offsets)

def main():
    if len(sys.argv) < 3:
        print("Usage: map_crt.py <reference_exe> <target_exe> [gen_dir] [module_name]")
        print("Example: map_crt.py work/ST1_UNP.EXE work/TIT_UNP2.EXE gen tit")
        sys.exit(1)
    
    ref_path = sys.argv[1]
    target_path = sys.argv[2]
    gen_dir = sys.argv[3] if len(sys.argv) > 3 else None
    module_name = sys.argv[4] if len(sys.argv) > 4 else None
    
    print(f"Reference: {ref_path}")
    print(f"Target: {target_path}")
    
    ref_image, ref_info = read_exe(ref_path)
    target_image, target_info = read_exe(target_path)
    
    ref_crt_seg = ST1_CRT_SEG
    target_crt_seg = target_info['cs']
    
    print(f"Reference CRT seg: 0x{ref_crt_seg:04X}")
    print(f"Target CRT seg: 0x{target_crt_seg:04X}")
    print()
    
    # Map functions by signature matching
    mapped = map_crt_functions(ref_image, ref_crt_seg, target_image, target_crt_seg)
    
    # Get called offsets from generated code
    called_offsets = []
    if gen_dir and module_name:
        called_offsets = extract_called_offsets(gen_dir, module_name, target_crt_seg)
        print(f"CRT offsets called by {module_name}: {len(called_offsets)}")
    
    print(f"\nMapped {len(mapped)} functions:")
    for off in sorted(mapped.keys()):
        marker = " <-- CALLED" if off in called_offsets else ""
        print(f"  0x{off:04X}: {mapped[off]}{marker}")
    
    # Show unmapped called offsets
    if called_offsets:
        unmapped = [off for off in called_offsets if off not in mapped]
        if unmapped:
            print(f"\nUnmapped offsets still called ({len(unmapped)}):")
            for off in unmapped:
                print(f"  0x{off:04X}: ???")
    
    # Generate C code for crt_shims.c
    print(f"\n// C code for {module_name or 'module'} CRT shims:")
    print(f'        {{ "{module_name or "module"}", 0x{target_crt_seg:04X}, {{', end='')
    
    # Map to the 30-element array format used in crt_shims.c
    crt_order = [
        "exit", "fclose", "fopen", "fread", "fwrite", "printf",
        "_fmalloc", "_ffree", "_fstrlen", "fseek", "sprintf", "rand",
        "inp", "outp", "_fmemcpy", "_fmemset", "_dos_allocmem",
        "abs", "rand", "spawnl", "_chain_intr", "_dos_getvect",
        "_dos_setvect", "_aFlmul", "_aFlrem", "_aFlrem", "_aFlshl", "_aFlshr", "_aFldiv", "_aFlrem"
    ]
    
    # Reverse map
    name_to_off = {v: k for k, v in mapped.items()}
    
    offsets = []
    for name in crt_order:
        off = name_to_off.get(name, 0)
        offsets.append(f"0x{off:04X}")
    
    print(", ".join(offsets) + "} },")

if __name__ == '__main__':
    main()
