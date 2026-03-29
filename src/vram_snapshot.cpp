#include "vram_snapshot.h"
#include <stdio.h>
#include <string.h>

static void write_le32(unsigned char* d, unsigned u) {
    d[0] = (unsigned char)(u & 0xff);
    d[1] = (unsigned char)((u >> 8) & 0xff);
    d[2] = (unsigned char)((u >> 16) & 0xff);
    d[3] = (unsigned char)((u >> 24) & 0xff);
}

static unsigned read_le32(const unsigned char* d) {
    return (unsigned)d[0] | ((unsigned)d[1] << 8) | ((unsigned)d[2] << 16) | ((unsigned)d[3] << 24);
}

extern "C" int vramSnapshotWriteFile(const char* path, const UInt8* vram, const UInt8* regs, const UInt16* palette,
                                     int display_on, int screen_mode) {
    if (!path || !vram || !regs || !palette) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;

    unsigned char hdr[VRAM_SNAPSHOT_HEADER_BYTES];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, VRAM_SNAPSHOT_MAGIC, 8);
    write_le32(hdr + 8, VRAM_SNAPSHOT_VERSION);
    write_le32(hdr + 12, VRAM_SNAPSHOT_VRAM_SIZE);
    write_le32(hdr + 16, VRAM_SNAPSHOT_REG_BYTES);
    hdr[20] = display_on ? (unsigned char)1 : (unsigned char)0;
    hdr[21] = (unsigned char)(screen_mode & 0xff);
    for (int i = 0; i < 16; i++) {
        UInt16 p = palette[i];
        hdr[24 + i * 2] = (unsigned char)(p & 0xff);
        hdr[24 + i * 2 + 1] = (unsigned char)((p >> 8) & 0xff);
    }

    int ok = (fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && fwrite(vram, 1, VRAM_SNAPSHOT_VRAM_SIZE, f) == VRAM_SNAPSHOT_VRAM_SIZE &&
              fwrite(regs, 1, VRAM_SNAPSHOT_REG_BYTES, f) == VRAM_SNAPSHOT_REG_BYTES);
    fclose(f);
    return ok ? 1 : 0;
}

extern "C" int vramSnapshotReadFile(const char* path, UInt8* vram, UInt8* regs, UInt16* palette, int* display_on,
                                    int* screen_mode, int* version_out) {
    if (!path || !vram || !regs || !palette || !display_on || !screen_mode) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    unsigned char hdr[VRAM_SNAPSHOT_HEADER_BYTES];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        return 0;
    }
    if (memcmp(hdr, VRAM_SNAPSHOT_MAGIC, 8) != 0) {
        fclose(f);
        return 0;
    }
    unsigned ver = read_le32(hdr + 8);
    unsigned vram_sz = read_le32(hdr + 12);
    unsigned reg_sz = read_le32(hdr + 16);
    if (ver != VRAM_SNAPSHOT_VERSION || vram_sz != VRAM_SNAPSHOT_VRAM_SIZE || reg_sz != VRAM_SNAPSHOT_REG_BYTES) {
        fclose(f);
        return 0;
    }
    *display_on = hdr[20] ? 1 : 0;
    *screen_mode = (int)hdr[21];
    if (version_out) *version_out = (int)ver;
    for (int i = 0; i < 16; i++)
        palette[i] = (UInt16)((unsigned)hdr[24 + i * 2] | ((unsigned)hdr[24 + i * 2 + 1] << 8));

    if (fread(vram, 1, VRAM_SNAPSHOT_VRAM_SIZE, f) != VRAM_SNAPSHOT_VRAM_SIZE ||
        fread(regs, 1, VRAM_SNAPSHOT_REG_BYTES, f) != VRAM_SNAPSHOT_REG_BYTES) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}
