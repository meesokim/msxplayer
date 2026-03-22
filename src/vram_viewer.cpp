#include "msxplay.h"
#include <stdio.h>
#include <string.h>

extern "C" UInt8* vdpGetVramPtr();

// Simple TMS9918 palette (RGB565)
static UInt16 tms_palette[] = {
    0x0000, 0x0000, 0x3CB9, 0x747A, 0x595C, 0x83B1, 0xB92A, 0x659F,
    0xDB2B, 0xFF9F, 0xCCCB, 0xDE90, 0x3A24, 0xB336, 0xCCCC, 0xFFFF
};

void updateVramViewer(SDL_Renderer* renderer, SDL_Texture* texture) {
    UInt8* vram = vdpGetVramPtr();
    if (!vram || !renderer || !texture) return;

    static UInt16 temp[256 * 256];
    memset(temp, 0, sizeof(temp));

    // Screen 2 has 3 sets of 256 tiles
    for (int ty = 0; ty < 16; ty++) {
        for (int tx = 0; tx < 16; tx++) {
            int tileIdx = ty * 16 + tx;
            for (int py = 0; py < 8; py++) {
                UInt8 pattern = vram[0x0000 + tileIdx * 8 + py];
                UInt8 color = vram[0x2000 + tileIdx * 8 + py];
                int fg = color >> 4;
                int bg = color & 0x0F;
                for (int px = 0; px < 8; px++) {
                    int bit = (pattern >> (7 - px)) & 1;
                    temp[(ty * 8 + py) * 256 + (tx * 8 + px)] = tms_palette[bit ? fg : bg];
                }
            }
        }
    }

    SDL_UpdateTexture(texture, NULL, temp, 256 * 2);
}
