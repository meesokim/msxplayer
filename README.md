# MSXPlayer

A lightweight, standalone MSX1/2 emulator core integrated with SDL2, featuring hardware-accurate rendering and a compact footprint.

## Features

- **Hardware-Accurate Rendering**: 
  - Manual high-performance implementation of TMS9918A Screen Modes 0, 1, and 2.
  - Precise bitmasking for pattern and color tables, ensuring 100% compatibility with games like *Galaga*.
  - Full support for 16x16 and 32x32 (magnified) sprites with Early Clock (EC) bit clipping.
- **Robust Input System**:
  - Comprehensive mapping of the full MSX keyboard matrix (Row 0-8).
  - Integrated PSG Port A joystick emulation for maximum game compatibility.
- **Audio Support**:
  - Integrated PSG (AY-8910) audio mixer with SDL2 output.
- **Diagnostic Tools**:
  - **VRAM Capture**: Press `Print Screen` to dump VRAM to `capture.sc2` and take a screenshot to `capture.bmp`.
  - **VRAM Viewer**: Real-time tile/pattern visualization via the `-v` command-line option.
  - **Debug Mode**: Detailed VDP and I/O logging via the `-d` option.
- **Portability & Ease of Use**:
  - **ZIP Support**: Directly load ROM files from ZIP archives.
  - **Fully Static Build**: The Windows version (`msxplay.exe`) is built as a single executable with no external DLL requirements.

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
