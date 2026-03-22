#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef uint8_t  UInt8;
typedef uint16_t UInt16;

static UInt16 msx1_palette[] = {
    0x0000, 0x0000, 0x3CB9, 0x747A, 0x595C, 0x83B1, 0xB92A, 0x659F,
    0xDB2B, 0xFF9F, 0xCCCB, 0xDE90, 0x3A24, 0xB336, 0xCCCC, 0xFFFF
};

int main() {
    FILE* f = fopen("capture.sc2", "rb");
    if (!f) return 1;
    fseek(f, 7, SEEK_SET); // Skip MSX header
    UInt8 vram[16384];
    fread(vram, 1, 16384, f);
    fclose(f);

    // Standard Screen 2 mapping for a dump
    int nameTab = 0x1800;
    int patternTab = 0x0000;
    int colorTab = 0x2000;

    static UInt16 pixels[256 * 192];
    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 256; x++) {
            int tileIdx = (y / 8) * 32 + (x / 8);
            int charCode = vram[nameTab + tileIdx];
            int zone = y / 64;
            int finalIdx = (zone << 8) | charCode;

            UInt8 pattern = vram[patternTab + finalIdx * 8 + (y % 8)];
            UInt8 color = vram[colorTab + finalIdx * 8 + (y % 8)];
            
            int bit = (pattern >> (7 - (x % 8))) & 1;
            int colIdx = bit ? (color >> 4) : (color & 0x0F);
            if (colIdx == 0) colIdx = 4; // Use some color for background
            pixels[y * 256 + x] = msx1_palette[colIdx];
        }
    }

    // Save as simple BMP
    FILE* out = fopen("check_sc2.bmp", "wb");
    uint16_t bfType = 0x4D42;
    uint32_t bfSize = 54 + 256 * 192 * 2;
    uint32_t bfReserved = 0;
    uint32_t bfOffBits = 54;
    fwrite(&bfType, 2, 1, out);
    fwrite(&bfSize, 4, 1, out);
    fwrite(&bfReserved, 4, 1, out);
    fwrite(&bfOffBits, 4, 1, out);

    uint32_t biSize = 40;
    int32_t biWidth = 256;
    int32_t biHeight = -192; // Top-down
    uint16_t biPlanes = 1;
    uint16_t biBitCount = 16;
    uint32_t biCompression = 0;
    uint32_t biSizeImage = 256 * 192 * 2;
    int32_t biXPelsPerMeter = 0;
    int32_t biYPelsPerMeter = 0;
    uint32_t biClrUsed = 0;
    uint32_t biClrImportant = 0;
    fwrite(&biSize, 4, 1, out);
    fwrite(&biWidth, 4, 1, out);
    fwrite(&biHeight, 4, 1, out);
    fwrite(&biPlanes, 2, 1, out);
    fwrite(&biBitCount, 2, 1, out);
    fwrite(&biCompression, 4, 1, out);
    fwrite(&biSizeImage, 4, 1, out);
    fwrite(&biXPelsPerMeter, 4, 1, out);
    fwrite(&biYPelsPerMeter, 4, 1, out);
    fwrite(&biClrUsed, 4, 1, out);
    fwrite(&biClrImportant, 4, 1, out);

    fwrite(pixels, 2, 256 * 192, out);
    fclose(out);
    printf("check_sc2.bmp created.\n");
    return 0;
}
