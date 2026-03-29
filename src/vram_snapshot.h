#ifndef VRAM_SNAPSHOT_H
#define VRAM_SNAPSHOT_H

#include "MsxTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRAM_SNAPSHOT_MAGIC "MSXVRAM1"
#define VRAM_SNAPSHOT_VERSION 1
#define VRAM_SNAPSHOT_HEADER_BYTES 64
#define VRAM_SNAPSHOT_VRAM_SIZE 0x4000
#define VRAM_SNAPSHOT_REG_BYTES 64

/** Write snapshot (same interpretation as msx1RenderFrameToRgb565). Returns 1 on success. */
int vramSnapshotWriteFile(const char* path, const UInt8* vram, const UInt8* regs, const UInt16* palette,
                          int display_on, int screen_mode);

/**
 * Read snapshot. vram must hold VRAM_SNAPSHOT_VRAM_SIZE, regs VRAM_SNAPSHOT_REG_BYTES, palette 16 entries.
 * Returns 1 on success. On failure, contents are undefined.
 */
int vramSnapshotReadFile(const char* path, UInt8* vram, UInt8* regs, UInt16* palette, int* display_on,
                         int* screen_mode, int* version_out);

#ifdef __cplusplus
}
#endif

#endif
