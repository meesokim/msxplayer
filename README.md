# MSXPlayer

A lightweight, standalone MSX1/2 emulator core integrated with SDL2, featuring hardware-accurate rendering and a compact footprint.

## Features

- **Hardware-Accurate Rendering**:
  - TMS9918A Screen Modes 0, 1, 2, and 3 (multicolor).
  - **Screen 2 (Graphics II)**: Pattern/color fetches match blueberryMSX / VDP behavior (`chrGenBase & index`, `colTabBase & index`). The index must not be pre-masked with VRAM size before the AND with table bases (fixes bottom-third and wide-area glitches vs. split `cb`/`cm` shortcuts).
  - Full support for 16×16 and 32×32 (magnified) sprites with Early Clock (EC) bit clipping.
- **MegaROM / Mapper Support**:
  - **Konami**, **Konami SCC**, **ASCII8**, **ASCII16**, **ASCII16 SRAM**, **MSX-DOS 2 / MSX-WRITE**, **PAGE2** (8 KiB ROM at `8000h`), **mirrored** small ROMs, and **R-Type (Irem)**-style **RTYPE** mapper (openMSX `RomRType`: 16 KiB banks, fixed `4000h–7FFFh` page, switchable `8000h–BFFFh`).
  - **Mapper database** (`mapper_db.csv`): SHA-1 rows with **mapper**, **basic vs C-BIOS family** (`basic` → MSX BASIC machines / `none` → C-BIOS-style), and **font** (`e` intl / `j` JP). The menu syncs the highlighted ROM’s profile into the active BIOS choice; **F12** in emulation writes the **current mapper + session BIOS** back into the CSV (and refreshes the in-memory DB / directory index when used).
  - **Ctrl+F5**: Cycle mapper mode when a ROM is not in the database (includes `RTYPE`, megaROM order, etc.).
- **Enhanced User Experience**:
  - **Game Selection Menu**: ROM browser with keyboard navigation; **B** cycles BIOS preset for the next launch; **Ctrl+C** copies the resolved full path of the highlighted ROM.
  - **BIOS loader** (`bios_loader`): Modes include embedded fallback, **C-BIOS** (intl / main+logo / JP), **Philips VG-8020**-style, and **Sony HB-10**-style BASIC ROM layouts. `biosLoaderInit` snapshots linked BIOS assets before ROM swaps.
  - **openMSX compare (F9)**: Spawns a **detached** openMSX process (no blocking `system()` shell): expects `openMSX/derived/openmsx` (Linux) or `openMSX\derived\openmsx.exe` (Windows) next to the working directory. Passes **`-cart`** with a canonical path and **`-machine`** for VG-8020 / HB-10–aligned runs when those BIOS modes are active. Works from the **menu** (selected game + menu BIOS) and from **emulation** (loaded ROM + session BIOS).
  - **Issue tagging**: **E** marks / **U** unmarks the current ROM by SHA-1 (`game_issue_tags`); in emulation, **E** also writes a timestamped **PNG + `.vram` snapshot** under `issue_captures/` (each press, for diagnosis).
  - **Last Game Persistence**: `last_game.txt`.
  - **Fullscreen**: `Alt+Enter`.
  - **Scanlines**: `F8`.
- **Standalone / BIOS**: Embedded BIOS paths and loader behavior aimed at portable builds.
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
  - **verify_core** / **verify_tools**: Mapper and VRAM-focused checks (`make verify`, `make verify-tools`).
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

**F9** looks for a built openMSX binary at `openMSX/derived/openmsx` (or `openMSX\derived\openmsx.exe` on Windows) relative to the **current working directory**, so launch msxplay from the project root if you keep openMSX there.

### Controls

**In game**

- **Arrows** — movement  
- **SPACE / Z** — button A  
- **X** — button B  
- **1 / 2** — start (1P / 2P)  
- **Print Screen** — VRAM dump + BMP screenshot  
- **F6** — soft reset  
- **F7** — return to game menu (if a ROM list exists)  
- **F8** — toggle scanlines  
- **F9** — launch **openMSX** with the same cartridge and session BIOS (detached; see Features)  
- **F12** — save current ROM’s **mapper + BIOS profile** to `mapper_db.csv`  
- **Ctrl+F5** — cycle mapper (if ROM not in DB) and reset  
- **E** — mark ROM in issue set; saves PNG / VRAM snapshot under `issue_captures/`  
- **Alt+Enter** — fullscreen  
- **Alt+F4** — quit  
- **ESC** — quit  

**In menu**

- **Arrows / PgUp / PgDn** — move selection  
- **Enter** / **Space** — run selected ROM (**Alt+Enter** is fullscreen only, does not launch)  
- **B** — cycle BIOS preset for the next run  
- **Ctrl+C** — copy full path of highlighted ROM  
- **E** / **U** — mark / unmark issue tag for highlighted ROM  
- **F9** — openMSX compare for highlighted ROM (uses menu BIOS)  

## Project Architecture

Core emulation (R800, VDP, PSG, etc.) is derived from **blueMSX**-family sources; this project adds the SDL2 platform layer, menu, mapper database, and focused video/IO glue.
