# Fox Ranger II: Second Mission (1993, Soft Action) — Reconnaissance Delta Report

## Executive Summary

Fox Ranger II: Second Mission (1993) is built upon the same core engine foundations as Fox Ranger 1 (1992)—sharing identical planar VGA Mode Y video routines, font data, sprite descriptor structures, and Johab text encoding—but undergoes a major structural evolution from Fox Ranger 1's monolithic design into a **modular, multi-executable architecture** with an updated audio subsystem supporting native MPU-401 MIDI.

| Subsystem | Fox Ranger 1 (1992) | Fox Ranger 2 (1993) | Reusability / Delta Status |
|---|---|---|---|
| **Binary Model** | Monolithic `FOX.EXE` (128 KB unpacked) | Modular: 28 discrete MZ executables (`FR2.EXE` launcher, stages, cutscenes) | **Architectural change**: multi-binary dispatch via `FR.REF` and command-line tokens |
| **Packing** | PKLITE 1.12 + MS EXEPACK double pack | PKLITE 1.15/1.20 (some double packed with EXEPACK) | **Compatible**: Unpacked cleanly with `unpack_exe.py` |
| **Copy Protection** | Root files `\EV21.SYS` & `\EV22.SYS` (findfirst size checks) | None in root filesystem; uses CLI token validation (`0 S K L`) + `PWD.EXE` manual lookup | **Changed**: Keyfiles eliminated; bypass sub-exe parameter checks |
| **Video Engine** | 4-plane unchained Mode Y (320x200x256, 64 KB/plane) | 4-plane unchained Mode Y (320x200x256, 64 KB/plane) | **100% Identical**: `vga_modex.c` reusable as-is |
| **Fonts** | 11,080 B inside `FOX.DAT` (8x8, 8x16, Johab Hangul) | 11,080 B standalone in `FOX2.DAT` (`BOX.DAT` clone) | **100% Bit-for-bit Identical** font tables |
| **Palettes** | 768 B in `FOX.DAT` | 53 standalone `.COL` files (768 B each) | **Format identical**: decoupled per stage/scene |
| **Sprite Formats** | Single `VSPRITE.PAT` (208 sprites, 6B EOF dir, RLE) | 97 modular `.PAT` files (6B EOF dir, mostly raw uncompressed) | **Descriptor identical** (11B); loader handles variable sprite counts |
| **Script & Text** | Raw Johab in DGROUP of `FOX.EXE` | Raw Johab in DGROUP of cutscene binaries (`PRO*`, `VIS*`, `EPIL`) | **Identical encoding** (Johab KSSM); far pointer tables per cutscene binary |
| **Audio Hardware** | AdLib OPL2 (388h/389h) + SB CT-VOICE (220h, DMA1) | AdLib + SB CT-VOICE + **Native MPU-401 MIDI (330h/331h)** | **Extended**: Added MT-32/SC-55 MPU-401 support; 4 sound driver packs (A, S, M, I) |
| **Timer & Keyboard** | INT 08h (72.8 Hz, chains BIOS), INT 09h (port 60h) | INT 08h (PIT ch0, chains BIOS), INT 09h (port 60h) | **Identical in architecture**: tick counter + raw scancode buffer |

---

## 1. Executable Architecture & Packing Analysis

### 1.1 Multi-Executable Architecture
Unlike Fox Ranger 1 which ran entirely within a single monolithic executable (`FOX.EXE`), Fox Ranger 2 is split into **28 distinct MZ executables**, organized into functional subsystems:

1. **Master Dispatcher / Launcher**:
   - `FR2.EXE` (9,560 B, unpacked to `work/FR2_UNP.EXE` / `work/FOX2_UNP.EXE`): Invoked via `FRII.BAT` with the syntax `FR2 <I | A | M | S>` (where arguments select Internal PC Speaker, AdLib, MPU-401 MIDI, or Sound Blaster).
   - Coordinates gameplay by spawning stage and cutscene binaries in sequence using `spawnl(P_WAIT, ...)` and tracking persistent game state via `FR.REF`.

2. **Game Title, Menus & Utilities**:
   - `TIT.EXE`: Game title sequence.
   - `START.EXE`: Game start menu and mission briefings.
   - `PWD.EXE`: Password and continue system.
   - `SCORE.EXE`: High-score rankings table.
   - `SO.EXE`: Sound options and sound test.
   - `OVE.EXE`: Game over sequence.

3. **Gameplay Stage Engines**:
   - `ST1.EXE` (52,621 B): Stage 1 gameplay engine.
   - `ST2.EXE`, `ST22.EXE`: Stage 2 sections.
   - `ST3.EXE`, `ST32.EXE`: Stage 3 sections.
   - `ST4.EXE`, `ST42.EXE`: Stage 4 sections.
   - `ST5.EXE`: Stage 5 section.
   - `ST6.EXE`, `ST62.EXE`, `ST63.EXE`: Stage 6 sections.
   - `FOX2B.EXE` (52,299 B): Bonus stage engine.

4. **Cinematic & Narrative Cutscenes**:
   - `PRO1.EXE` through `PRO4.EXE`: Prologue animated cutscenes.
   - `VIS1.EXE`, `VIS2.EXE`, `VIS4.EXE`, `VIS5.EXE`: Mid-game visual narrative interludes.
   - `EPIL.EXE`: Epilogue story sequence.

5. **Inter-Process State Persistence (`FR.REF`)**:
   - A fixed **38-byte binary file** (`FR.REF`) read (`rb`) and updated (`wb`) between executable transitions. Contains stage progression indices, sound configuration, score buffers, and player lives.

### 1.2 Compression & Unpacking
Every executable was packed for commercial floppy release:
- **Packer Identification**: All binaries feature the signature `PKLITE Copr. 1990-91 PKWARE Inc. All Rights Reserved` at header offset `0x1E` with entry CS:IP `FFF0:0100`.
- **Packaging Variations**:
  - *Single Packed (PKLITE only)*: `FR2.EXE`, `FOX2B.EXE`, `ST1.EXE`, `PRO1.EXE`..`PRO4.EXE`, `START.EXE`.
  - *Double Packed (PKLITE wrapping Microsoft EXEPACK)*: `EPIL.EXE`, `OVE.EXE`, `PWD.EXE`, `SCORE.EXE`, `SO.EXE`, `ST2..ST63`, `TIT.EXE`, `VIS1..VIS5`.
- **Unpacking Verification**:
  All 28 executables have been fully unpacked and relocations recovered into `work/*_UNP.EXE` (and `work/*_UNP2.EXE` for EXEPACK layers) using the Unicorn-based emulation tool `tools/unpack_exe.py`. The main unpacked launcher executable is preserved at `work/FOX2_UNP.EXE`.

### 1.3 Copy Protection Mechanisms
- **Root Keyfiles Removed**: Fox Ranger 1's `\EV21.SYS` (512 bytes) and `\EV22.SYS` (5100 bytes) checked via `_dos_findfirst` are **completely absent** in Fox Ranger 2.
- **Inter-Executable Parameter Verification**: Sub-executables (`ST1`, `PWD`, `VIS1`, etc.) inspect `argv` parameters on startup. If not launched by `FR2.EXE` with the secret command-line tokens (e.g., `0 S K L` or `L s C`), they invoke `exit(0)` immediately.
- **Password Routine (`PWD.EXE`)**: `PWD.EXE` reads matrix coordinates from `PWD.DAT` (500 bytes) and requires entering numeric/alphanumeric codes from the printed Korean user manual.

---

## 2. Asset & Script Formats Comparison

### 2.1 Font Subsystem (100% Bit-for-bit Identical to FR1)
Fox Ranger 2 moves fonts from FR1's monolithic `FOX.DAT` into a standalone asset file: `FOX2.DAT` (11,080 bytes).
Comparison against Fox Ranger 1's font block at `FOX.DAT:0x834` confirms byte-for-byte identity:

| Font Component | Dimensions | Glyph Count | Byte Size | Format |
|---|---|---|---|---|
| **ASCII 8x8** | 8x8 px | 95 (0x20..0x7E) | 760 bytes (`0x2F8`) | 1 bpp row-major |
| **ASCII 8x16** | 8x16 px | 95 (0x20..0x7E) | 1,520 bytes (`0x5F0`) | 1 bpp row-major |
| **Johab Cho (initials)** | 16x16 px | 8 sets × 19 glyphs | 4,864 bytes | 32 B / glyph |
| **Johab Jung (vowels)** | 16x16 px | 2 sets × 21 glyphs | 1,344 bytes | 32 B / glyph |
| **Johab Jong (finals)** | 16x16 px | 3 sets × 27 glyphs | 2,592 bytes | 32 B / glyph |
| **Total Font Block** | — | — | **11,080 bytes** | Exact match |

*Note*: `BOX.DAT` in the distribution is an identical 11,080-byte duplicate of `FOX2.DAT`. `FOX2.FON` (10,848 bytes) is used exclusively by `INSTALL.EXE` during floppy installation.

### 2.2 Palette Subsystem
- **FR1**: Embedded 768-byte palette at `FOX.DAT:0x000`.
- **FR2**: Decoupled into **53 standalone `.COL` files** (768 bytes each, 256 RGB triplets in 6-bit VGA DAC format `0..63`). This enables dynamic palette switching per stage, cutscene, and dialogue screen (e.g. `ST1ORI.COL`, `STAGE1B.COL`, `TITLE.COL`, `BONUS.COL`).

### 2.3 Sprite Containers (`.PAT` Files)
- **FR1**: Single container `VSPRITE.PAT` (511 KB, 208 sprites) with directory located at `fseek(-0x4E0, SEEK_END)`.
- **FR2**: Replaced by **97 modular `.PAT` files** tailored to individual stages and scenes (`ST1ORI.PAT`, `A.PAT`, `B1.PAT`..`B6.PAT`, `FXB.PAT`, etc.).
- **Directory Layout**:
  - Directory entries are identical 6-byte records:
    ```c
    struct PatDirEntry {
        uint16_t width;
        uint16_t height;
        uint16_t packed_size; // 0 = raw uncompressed w*h
    };
    ```
  - **In-Memory Descriptor**:
    The runtime sprite descriptor allocated in RAM is **identical to Fox Ranger 1's 11-byte structure**:
    ```c
    struct SpriteDesc {
        uint16_t width;       // +0
        uint16_t height;      // +2
        uint16_t packed;      // +4
        uint8_t  flag_wanted; // +6
        uint16_t offset;      // +7
        uint16_t segment;     // +9
    }; // sizeof = 11 bytes
    ```
  - **Decompression Optimization**: In major stage files like `ST1ORI.PAT` (200 sprites), `packed_size == 0` for all sprites, meaning sprites are stored uncompressed and streamed straight into VRAM/heap without RLE decode overhead.

### 2.4 Story and Script Strings
- **Encoding**: 100% **Johab (KSSM / KS C 5601-1992-3)** mixed with ASCII. No Hangul text exists in data files; all story dialogue resides inside the executables' DGROUP / initialized data segments.
- **Cutscene Distribution**:
  Because cutscenes are standalone binaries, dialogue scripts are neatly isolated:
  - `VIS1_UNP2.EXE`: Dialogue between Jun and HQ.
  - `VIS2_UNP2.EXE`: Defense army report on base crisis.
  - `VIS4_UNP2.EXE`: Base auto-destruct countdown.
  - `VIS5_UNP2.EXE`: Begzer escape and Malan fleet warning.
  - `PRO3_UNP.EXE` / `PRO4_UNP.EXE`: Prologue recovery scenes.
  - `EPIL_UNP2.EXE`: Epilogue ending monologue.
- **Pointer Table Architecture**:
  Dialogue lines are referenced via FAR pointer arrays `(uint16_t offset, uint16_t segment)`.
  *Verified mapping example (`VIS1_UNP2.EXE`)*:
  - Pointer table base: image offset `0xF650` (10 consecutive FAR pointers).
  - Target strings: image offsets `0x10074` through `0x10157`.
  - Localization to English can be accomplished by expanding the text block and updating the table offsets without modifying code routines.

---

## 3. Hardware & Driver Differences

### 3.1 Video Architecture (100% Reusable)
Disassembly of `ST1_UNP.EXE` (routine `00028:00089`) confirms that Fox Ranger 2 uses the exact same planar VGA Mode Y implementation as Fox Ranger 1:
```asm
mov ax, 0013h; int 10h             ; Standard Mode 13h (320x200x256)
mov dx, 03C4h; mov al, 04h; out dx, al
inc dx; in al, dx; and al, 0F7h; or al, 04h; out dx, al ; SEQ 04h: Chain-4 OFF
mov dx, 03CEh; mov al, 05h; out dx, al
inc dx; in al, dx; and al, 0EFh; out dx, al             ; GC 05h: Odd/Even OFF
dec dx; mov al, 06h; out dx, al
inc dx; in al, dx; and al, 0FDh; out dx, al             ; GC 06h: Chain OFF
mov dx, 03C4h; mov ax, 0F02h; out dx, ax               ; Map Mask: all 4 planes
mov ax, 0A000h; mov es, ax; xor di, di; rep stosw       ; Clear all 4x64 KB planes
mov dx, 03D4h; mov al, 14h; out dx, al
inc dx; in al, dx; and al, 0BFh; out dx, al             ; CRTC 14h: Dword mode OFF
dec dx; mov al, 17h; out dx, al
inc dx; in al, dx; and al, 40h; out dx, al              ; CRTC 17h: Byte mode ON
```
**Conclusion**: `src/vga_modex.c` and `src/vga_modex.h` require zero changes for FR2.

### 3.2 Audio Architecture & Native MIDI Support
While Fox Ranger 1 only shipped with AdLib music data (`ADLIB.DAT`), Fox Ranger 2 ships 4 distinct audio soundpacks:
1. **AdLib OPL2 (`A.ZZZ`)**: Uses ports `0x388` and `0x389` (compatible with `src/audio_opl.c`).
2. **Sound Blaster Voice (`S.ZZZ`)**: Embedded Creative CT-VOICE driver (Creative Technology 1989-1990) utilizing DSP reset/status ports `0x226`..`0x22E` and 8237 DMA Channel 1 (ports `0x02`, `0x03`, `0x0A`, `0x0B`, `0x0C`, `0x83`).
3. **Roland MT-32 & General MIDI / SC-55 (`M.ZZZ`)**:
   - **New Feature in FR2**: Disassembly reveals direct communication with **MPU-401 UART mode at ports `0x330` (Data) and `0x331` (Status/Command)**.
   - The game sends MIDI status sequences and SysEx initialization dumps for MT-32 and SC-55 sound modules.
4. **Internal PC Speaker (`I.ZZZ`)**: 1-bit timer channel 2 beeper routines via port `0x61` and `0x42`.

### 3.3 Timer and Keyboard Interrupt Entry Points
Disassembly of `ST1_UNP.EXE` locates the active hardware interrupt service routines:
- **Timer ISR (INT 08h)**:
  - Installed via `_dos_setvect(0x08, 0938:0131)` at `ST1:093b2`.
  - Re-programs PIT 8253/8254 Channel 0 (ports `0x43`, `0x40`).
  - Maintains a 32-bit tick counter at `[DS:00AC]` and chains to previous BIOS vector (`094e9: lcall [dbd:a22]`).
- **Keyboard ISR (INT 09h)**:
  - Installed via `_dos_setvect(0x09, 0041:0135)` at `ST1:00537`.
  - Reads raw XT set-1 scancodes from port `0x60` (`ST1:0055b`).
  - Stores the scancode into `[DS:2EE4]` and dispatches to a jump table (`ST1:0057a`) to maintain active key states.
  - Exactly corresponds to the SDL2 keyboard injection model in `src/io.c` and `src/platform.c`.

---

## 4. Reusability Roadmap from Fox Ranger 1 Harness

| Subsystem / Source File | Reuse Assessment | Required Adaptation for Fox Ranger 2 |
|---|---|---|
| `src/vga_modex.c` / `.h` | **100% Reusable** | None. Mode Y registers and planar logic are identical. |
| `src/retro_font.c` / `.h` | **100% Reusable** | None. Glyphs and Johab font composition are bit-for-bit identical. |
| `src/audio_opl.c` / Nuked-OPL3 | **100% Reusable** | Reusable for AdLib playback (`A.ZZZ` sound pack). |
| `src/ctvoice.c` / `.h` | **Reusable** | Reusable for Sound Blaster voice samples. |
| `src/io.c` / `.h` | **Extend** | Add port `0x330`/`0x331` MPU-401 passthrough or software MIDI synthesizer. |
| `src/mz_load.c` / `mem.c` | **Extend** | Support loading multiple stage/scene MZ modules or chaining them dynamically. |
| `src/assets.c` / `.h` | **Update** | Adapt `vsprite_load` to handle variable-count `.PAT` files and direct uncompressed sprite loading (`packed == 0`). Update palette loader to read `.COL` files. |
| `src/launcher.c` | **Update** | Support launching stages directly or implementing an `FR2.EXE` state-machine harness (`FR.REF`). |
| `tools/unpack_exe.py` | **100% Reusable** | Successfully unpacked all 28 binaries. |
| `tools/kscan.py` | **100% Reusable** | Successfully parsed Johab strings. |
