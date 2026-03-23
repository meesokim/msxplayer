# MSXPlayer

A lightweight, standalone MSX1/2 emulator core integrated with SDL2, featuring hardware-accurate rendering and a compact footprint.

## Features

- **Hardware-Accurate Rendering**: 
  - Manual high-performance implementation of TMS9918A Screen Modes 0, 1, and 2.
  - Precise bitmasking for pattern and color tables.
  - Full support for 16x16 and 32x32 (magnified) sprites with Early Clock (EC) bit clipping.
- **MegaROM Support**: 
  - Integrated ROM bank-switching for MegaROMs.
  - Support for **Konami** and **ASCII 8K** mappers, enabling play of larger titles like *Vampire Killer*, *Metal Gear*, and *Nemesis* series.
- **Enhanced User Experience**:
  - **Game Selection Menu**: Built-in interactive ROM browser with keyboard navigation.
  - **CBIOS Integration**: Automatically uses CBIOS for legal, out-of-the-box MSX1 emulation.
  - **Last Game Persistence**: Remembers the last game played for quick resumption.
  - **Fullscreen Toggle**: Press `Alt + Enter` to switch between windowed and fullscreen modes.
  - **Scanline Effect**: CRT-style scanline overlay for an authentic retro feel (Toggle with `F8`).
- **Standalone Executable**: BIOS ROMs are now embedded directly into the executable, removing the need for external `cbios_*.rom` files and making the emulator truly portable.
- **Enhanced ROM Compatibility**:
  - Improved mapper detection logic to correctly handle standard 32KB ROMs (e.g., *Yokai Yashiki*).
  - Added support for 16KB ROM mirroring to fix graphical issues in games like *Dig Dug*.
  - Added support for header-less 16KB ROMs that start at address `0x0000` (e.g., *Zenji*).
- **Robust Input System**:
  - Comprehensive mapping of the full MSX keyboard matrix.
  - Integrated PSG Port A joystick emulation mapped to Arrow keys and Space/Z/X.
- **Performance Optimizations**:
  - **HLE VDP Hooks**: High-Level Emulation for VRAM block transfers (`LDIRVM`), significantly boosting rendering speed for supported games.
- **Audio Support**:
  - Integrated PSG (AY-8910) audio mixer with SDL2 output.
- **Diagnostic Tools**:
  - **VRAM Capture**: Press `Print Screen` to dump VRAM to `capture.sc2` and take a screenshot to `capture.bmp`.
  - **VRAM Viewer**: Real-time tile/pattern visualization via the `-v` command-line option.
  - **Debug Mode**: Detailed VDP and I/O logging via the `-d` option.
- **Portability**:
  - **ZIP Support**: Directly load ROM files from ZIP archives.
  - **Fully Static Build**: Single executable with no external DLL dependencies for Windows.
## Build Instructions

### Requirements
- GCC/G++ (Linux)
- MinGW-w64 (for Windows cross-compilation)
- SDL2 Development Libraries

### Building
```bash
# Build both Linux and Windows versions
make all

# Build only Linux version
make msxplay

# Build only Windows version
make msxplay.exe
```

## Usage

```bash
# Run a game
./msxplay "Game.rom"

# Run a game from a ZIP archive
./msxplay "Game.zip"

# Enable VRAM Viewer
./msxplay -v "Game.rom"

# Enable Debug Logging
./msxplay -d "Game.rom"
```

### Controls
- **Arrow Keys**: Directional movement
- **SPACE / Z**: Button A (Select / Fire)
- **X**: Button B
- **1 / 2**: Start game (1-player / 2-player)
- **Print Screen**: Capture VRAM and Screenshot
- **ESC**: Exit

## Project Architecture
This project leverages the core emulation logic from `blueMSX` (R800 CPU, VDP, and PSG cores) and provides a specialized SDL2-based platform layer for video, audio, and input handling.
