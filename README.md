# MSXPlayer

A lightweight, standalone MSX1/2 emulator core integrated with SDL2, featuring hardware-accurate rendering and a compact footprint.

## Features

- **Hardware-Accurate Rendering**:
  - TMS9918A Screen Modes 0, 1, 2, and 3 (multicolor).
  - **Screen 2 (Graphics II)**: Pattern/color fetches match blueberryMSX / VDP behavior (`chrGenBase & index`, `colTabBase & index`). The index must not be pre-masked with VRAM size before the AND with table bases (fixes bottom-third and wide-area glitches vs. split `cb`/`cm` shortcuts).
  - Full support for 16×16 and 32×32 (magnified) sprites with Early Clock (EC) bit clipping.
- **MegaROM / Mapper Support**:
  - **Konami**, **Konami SCC**, **ASCII8**, **ASCII16**, **ASCII16 SRAM**, **MSX-DOS 2 / MSX-WRITE**, **PAGE2** (8 KiB ROM at `8000h`), **mirrored** small ROMs, and **R-Type (Irem)**-style **RTYPE** mapper (openMSX `RomRType`: 16 KiB banks, fixed `4000h–7FFFh` page, switchable `8000h–BFFFh`).
  - **Mapper database** (`mapper_db.csv`): SHA-1–based profiles (mapper type, BIOS mode, font). Includes entries for **R-Type** and **Scion** (Mirrored) among others.
  - **Ctrl+F5**: Cycle mapper mode when a ROM is not in the database (includes `RTYPE`, megaROM order, etc.).
- **Enhanced User Experience**:
  - **Game Selection Menu**: ROM browser with keyboard navigation.
  - **CBIOS Integration**: Optional C-BIOS and other BIOS modes via loader.
  - **Last Game Persistence**: `last_game.txt`.
  - **Fullscreen**: `Alt+Enter`.
  - **Scanlines**: `F8`.
- **Standalone / BIOS**: Embedded BIOS paths and loader improvements for portable builds.
- **Enhanced ROM Compatibility**:
  - Mirrored `<64 KiB` ROM handling aligned with openMSX `RomPlain` heuristics (`g_mirroredFirstPage`).
  - Plain 16/32 KiB and header-based detection.
- **Input**: MSX keyboard matrix; PSG joystick mapped to arrows and Space/Z/X.
- **Performance**: HLE hooks for VRAM block transfers (`LDIRVM`) where applicable.
- **Audio**: AY-8910 (PSG), SCC when used by mapper.
- **Diagnostics**:
  - **Print Screen**: VRAM dump / screenshot.
  - **VRAM Viewer**: `-v`.
  - **Debug**: `-d`.
  - **verify_core** / **verify_tools**: Mapper and VRAM unit checks (`make verify`, `make verify-tools`).
- **Portability**:
  - ZIP loading.
  - **Linux** and **Windows** builds (MinGW cross-compile; see Makefile).

## blueberryMSX (upstream-style build)

The tree may include a **`blueberryMSX`** symlink to a full [blueberryMSX](https://github.com/pokebyte/blueberryMSX) checkout used for VDP/CPU reference. The Makefile there supports:

- **`make`** or **`make OS=linux`** — native Linux (`bluemsx`), SDL2 + libudev + OpenGL.
- **`make OS=mingw`** — Windows PE cross-compile (`bluemsx.exe`), MinGW-w64 + SDL2 MinGW dev tree; **`MINGW_SDL2`** should point at the `x86_64-w64-mingw32` folder from the official SDL2 MinGW development archive.
- **`make help`** — prints usage only (default goal is **`all`**, not `help`).
- Linux-only **`PiUdev.c`** is replaced by **`Src/Pi/PiUdevStub.c`** on MinGW builds.

## Build Instructions

### Requirements

- GCC/G++ (Linux)
- MinGW-w64 (Windows cross-compilation)
- SDL2 development libraries (`libsdl2-dev` on Debian/Ubuntu)
- Optional: `pkg-config` for SDL2 flags

### Building

```bash
# Linux + Windows binaries (see Makefile for msxplay.exe / MinGW deps)
make all

# Mapper / VRAM verification
make verify
make verify-tools

# Linux only
make msxplay

# Windows cross (requires deps/local_win or equivalent SDL2 MinGW layout)
make msxplay.exe
```

## Usage

```bash
./msxplay "Game.rom"
./msxplay "Game.zip"
./msxplay -v "Game.rom"
./msxplay -d "Game.rom"
```

### Controls

- **Arrows** — movement  
- **SPACE / Z** — button A  
- **X** — button B  
- **1 / 2** — start (1P / 2P)  
- **Print Screen** — capture  
- **Ctrl+F5** — cycle mapper (if ROM not in DB)  
- **ESC** — quit  

## Project Architecture

Core emulation (R800, VDP, PSG, etc.) is derived from **blueMSX**-family sources; this project adds the SDL2 platform layer, menu, mapper database, and focused video/IO glue.
