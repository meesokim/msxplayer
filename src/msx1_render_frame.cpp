#include "msx1_render_frame.h"

/* Register mix key: same as blueberryMSX updateScreenMode() first switch (MSX1). */
static int msx1RegMixKey(const UInt8* regs) {
    return ((regs[0] & 0x0e) >> 1) | (regs[1] & 0x18);
}

static int msx1Graphic2ChrGenBase(const UInt8* regs, int vramMask) {
    return (((int)regs[4] << 11) | (((int)regs[3] & 0x1F) << 6) | 0x3F) & vramMask;
}

static int msx1Graphic2ColTabBase(const UInt8* regs, int vramMask) {
    return (((int)regs[10] << 14) | ((int)regs[3] << 6) | 0x3F) & vramMask;
}

static void renderMSX1Screen0Plus(const UInt8* vram, const UInt8* regs, UInt16 pfg, UInt16 pbg, int vramMask,
                                  UInt16* fb) {
    const int chrTabBase = (((int)regs[2] << 10) | 0x3FF) & vramMask;
    const int chrGenBase = msx1Graphic2ChrGenBase(regs, vramMask);
    const int ntOrMask = (int)(0xFFFFF000u);

    for (int y = 0; y < 192; y++) {
        const unsigned yu = (unsigned)y;
        const int patternBase = (int)(0xFFFFE000u | ((yu & 0xC0u) << 5) | (yu & 7u));
        int shift = 0;
        int xChar = 0;
        int patternByte = 0;
        int dstOff = (y + 24) * 272 + 8;

        for (int tileX = 0; tileX < 32; tileX++) {
            if (tileX == 0 || tileX == 31) {
                for (int p = 0; p < 8; p++)
                    fb[dstOff++] = pbg;
            } else {
                for (int j = 0; j < 4; j++) {
                    if (shift <= 2) {
                        int charIdx = 0xC00 + 40 * (y / 8) + xChar++;
                        int ntAddr = (chrTabBase & (ntOrMask | charIdx)) & vramMask;
                        UInt8 code = vram[ntAddr];
                        int pAddr = (chrGenBase & (patternBase | (code * 8))) & vramMask;
                        patternByte = vram[pAddr];
                        shift = 8;
                    }
                    int bit = (patternByte >> (--shift)) & 1;
                    fb[dstOff++] = bit ? pfg : pbg;
                    bit = (patternByte >> (--shift)) & 1;
                    fb[dstOff++] = bit ? pfg : pbg;
                }
            }
        }
    }
}

static void renderMSX1Screen3(const UInt8* vram, const UInt8* regs, const UInt16* palette, int vramMask, UInt16* fb) {
    const int chrTabBase = (((int)regs[2] << 10) | 0x3FF) & vramMask;
    const int chrGenBase = (((int)regs[4] << 11) | 0x7FF) & vramMask;
    const int ntRowMask = (int)(0xFFFFFC00u);
    const int patLineMask = (int)(0xFFFFF800u);

    for (int y = 0; y < 192; y++) {
        const int ntRow = (chrTabBase & (ntRowMask | (32 * (y / 8)))) & vramMask;
        const int patternBase = (chrGenBase & (patLineMask | ((y >> 2) & 7))) & vramMask;
        int dstOff = (y + 24) * 272 + 8;
        for (int tx = 0; tx < 32; tx++) {
            const UInt8 code = vram[(ntRow + tx) & vramMask];
            const UInt8 colPat = vram[(patternBase | (code * 8)) & vramMask];
            const UInt16 fc = palette[colPat >> 4];
            const UInt16 bc = palette[colPat & 0x0F];
            for (int p = 0; p < 4; p++) fb[dstOff++] = fc;
            for (int p = 0; p < 4; p++) fb[dstOff++] = bc;
        }
    }
}

extern "C" void msx1RenderFrameToRgb565(const UInt8* vram, const UInt8* regs, const UInt16* palette, int display_on,
                                        int screen_mode, UInt16* fb) {
    if (!vram || !regs || !palette || !fb) return;

    int bgCol = regs[7] & 0x0F;
    UInt16 bgPixel = palette[bgCol];
    int vramMask = 0x3FFF;

    for (int i = 0; i < 272 * 240; i++) fb[i] = bgPixel;

    if (!display_on) return;

    const int mixKey = msx1RegMixKey(regs);
    int fg = regs[7] >> 4, bg = regs[7] & 0x0F;
    UInt16 pfg = palette[fg], pbg = palette[bg];

    /* Prioritize mode detection from registers (M1, M2, M3, M4, M5)
     * mixKey bits: bit 0=M3, bit 1=M4, bit 2=M5, bit 3=M2, bit 4=M1
     * 0x10: Text 1 (Screen 0)
     * 0x01 or 0x04: Graphic 2/3 (Screen 2/4)
     * 0x08: Multicolor (Screen 3)
     * 0x00: Graphic 1 (Screen 1)
     */
    if ((mixKey & 0x10) && (mixKey & 0x01)) {
        /* Screen 0+ hack */
        renderMSX1Screen0Plus(vram, regs, pfg, pbg, vramMask, fb);
    } else if (mixKey & 0x10) {
        /* Screen 0 (Text 1) */
        int nt = (regs[2] << 10) & vramMask, pb = (regs[4] << 11) & vramMask;
        for (int y = 0; y < 192; y++) {
            int py = y % 8, row = (y / 8) * 40, dstOff = (y + 24) * 272 + 16;
            for (int tx = 0; tx < 40; tx++) {
                UInt8 pat = vram[(pb + vram[(nt + row + tx) & vramMask] * 8 + py) & vramMask];
                for (int b = 0; b < 6; b++) fb[dstOff + tx * 6 + b] = (pat & (0x80 >> b)) ? pfg : pbg;
            }
        }
    } else if (mixKey & 0x05) {
        /* Screen 2 (Graphic 2) or Screen 4 (Graphic 3) */
        const int chrTabBase = ((int)regs[2] << 10) & vramMask;
        /* Screen 2/4: R#4 bits 0-1 are mask, bits 2-5 are base. R#3 bits 0-6 are mask, bit 7 is base. */
        const int chrGenBase = ((int)(regs[4] & 0x3C) << 11) & vramMask;
        const int chrGenMask = ((int)(regs[4] & 0x03) << 11) | 0x7FF;
        const int colTabBase = (((int)regs[10] << 14) | ((int)(regs[3] & 0x80) << 6)) & vramMask;
        const int colTabMask = ((int)(regs[3] & 0x7F) << 6) | 0x3F;

        for (int y = 0; y < 192; y++) {
            const int zone = (y & 0xC0) << 5;
            const int line = y & 7;
            const int row = (y / 8) * 32;
            int dstOff = (y + 24) * 272 + 8;
            for (int tx = 0; tx < 32; tx++) {
                const int ntAddr = (chrTabBase | (row + tx)) & vramMask;
                const UInt8 code = vram[ntAddr];
                const int pIdx = zone | (code << 3) | line;
                const UInt8 pat = vram[(chrGenBase | (pIdx & chrGenMask)) & vramMask];
                const UInt8 col = vram[(colTabBase | (pIdx & colTabMask)) & vramMask];
                UInt16 pf = palette[col >> 4], pb = palette[col & 0x0F];
                for (int b = 0; b < 8; b++) fb[dstOff + tx * 8 + b] = (pat & (0x80 >> b)) ? pf : pb;
            }
        }
    } else if (mixKey & 0x08) {
        /* Screen 3 (Multicolor) */
        renderMSX1Screen3(vram, regs, palette, vramMask, fb);
    } else {
        /* Screen 1 (Graphic 1) */
        int nt = (regs[2] << 10) & vramMask;
        int pb = (regs[4] << 11) & vramMask;
        int ct = (regs[3] << 6) & vramMask;
        for (int y = 0; y < 192; y++) {
            int ty = y / 8;
            int py = y % 8;
            int rowStart = ty * 32;
            int dstOff = (y + 24) * 272 + 8;
            for (int tx = 0; tx < 32; tx++) {
                int code = vram[(nt + rowStart + tx) & vramMask];
                UInt8 pat = vram[(pb + code * 8 + py) & vramMask];
                UInt8 col = vram[(ct + code / 8) & vramMask];
                pfg = palette[col >> 4];
                pbg = palette[col & 0x0F];
                for (int b = 0; b < 8; b++) {
                    fb[dstOff + tx * 8 + b] = (pat & (0x80 >> b)) ? pfg : pbg;
                }
            }
        }
    }

    int large = regs[1] & 0x02, mag = regs[1] & 0x01;
    int spriteTab = ((int)regs[5] << 7) & vramMask, spriteGen = ((int)regs[6] << 11) & vramMask;
    int size = mag ? (large ? 32 : 16) : (large ? 16 : 8);

    int numSprites = 32;
    for (int i = 0; i < 32; i++) {
        if (vram[spriteTab + i * 4] == 208) {
            numSprites = i;
            break;
        }
    }

    for (int i = numSprites - 1; i >= 0; i--) {
        int sy_raw = vram[spriteTab + i * 4];
        int sc_raw = vram[spriteTab + i * 4 + 3];
        int sc = sc_raw & 0x0F;
        if (!sc) continue;
        int sx = vram[spriteTab + i * 4 + 1];
        if (sc_raw & 0x80) sx -= 32;
        int si = vram[spriteTab + i * 4 + 2];
        if (large) si &= ~0x03;
        int sy = (sy_raw > 208) ? sy_raw - 255 : sy_raw + 1;
        UInt16 psc = palette[sc];
        for (int py = 0; py < size; py++) {
            int vY = sy + py;
            if (vY < 0 || vY >= 192) continue;
            int rowOff = (vY + 24) * 272, vPy = mag ? (py / 2) : py;
            for (int px = 0; px < size; px++) {
                int vX = sx + px;
                if (vX < 0 || vX >= 256) continue;
                int vPx = mag ? (px / 2) : px;
                int tOff = large ? ((vPx / 8) * 2 + (vPy / 8)) : 0;
                if ((vram[(spriteGen + (si + tOff) * 8 + (vPy % 8)) & 0x3FFF] >> (7 - (vPx % 8))) & 1)
                    fb[rowOff + (vX + 8)] = psc;
            }
        }
    }
}
