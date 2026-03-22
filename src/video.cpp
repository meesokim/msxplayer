#include "msxplay.h"
#include "FrameBuffer.h"
#include <stdio.h>
#include <string.h>

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* texture = NULL;

extern "C" void* ioPortGetRef(int port);
extern "C" void vdpForceSync();
extern "C" int vdpGetStatus(void* vdp, int reg);
extern "C" UInt16* getSharedVdpBuffer();

extern "C" FrameBuffer* frameBufferGetViewFrame();
extern "C" FrameBuffer* frameBufferGetDrawFrame();
extern "C" FrameBuffer* frameBufferFlipDrawFrame();
extern "C" FrameBuffer* frameBufferFlipViewFrame(int mixFrames);

static SDL_Window* vramWindow = NULL;
static SDL_Renderer* vramRenderer = NULL;
static SDL_Texture* vramTexture = NULL;

extern void updateVramViewer(SDL_Renderer* renderer, SDL_Texture* texture);

extern "C" UInt8* vdpGetVramPtr();
extern "C" UInt16* vdpGetPalettePtr();
extern "C" int vdpGetScreenOn();
extern "C" int vdpGetScreenMode();
extern "C" UInt8* vdpGetRegsPtr();

extern "C" void RefreshScreen(int screenMode) {
    if (!renderer || !texture) return;
    
    void* vdp_ref = ioPortGetRef(0x99);
    if (vdp_ref) vdpForceSync();

    UInt8* vram = vdpGetVramPtr();
    UInt16* palette = vdpGetPalettePtr();
    UInt8* regs = vdpGetRegsPtr();

    if (!vram || !palette || !regs) return;

    static int debugCount = 0;
    if (debugMode && debugCount++ % 120 == 0) {
        printf("VDP State: On=%d Mode=%d Regs: R0=%02X R1=%02X R2=%02X R3=%02X R4=%02X R5=%02X R6=%02X R7=%02X\n", 
            vdpGetScreenOn(), vdpGetScreenMode(),
            regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]);
        fflush(stdout);
    }

    static int vramCount = 0;
    if (vramViewerMode && vramWindow && vramRenderer && vramTexture && vramCount++ % 60 == 0) {
        updateVramViewer(vramRenderer, vramTexture);
        SDL_RenderClear(vramRenderer);
        SDL_RenderCopy(vramRenderer, vramTexture, NULL, NULL);
        SDL_RenderPresent(vramRenderer);
    } else { vramCount++; }

    void* pixels;
    int pitch;
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) == 0) {
        UInt16* dst = (UInt16*)pixels;
        int bgCol = regs[7] & 0x0F;
        int displayEnabled = vdpGetScreenOn();

        // Background / Blanking
        for (int i = 0; i < 272 * 240; i++) dst[i] = palette[bgCol];

        if (displayEnabled) {
            int vramMask = 0x3FFF; 
            int mode = vdpGetScreenMode();

            if (mode == 0) {
                // Screen 0 (Text Mode 40x24)
                int nameTable = (regs[2] << 10) & vramMask;
                int patternBase = (regs[4] << 11) & vramMask;
                int fgCol = (regs[7] >> 4);
                int bgColIdx = (regs[7] & 0x0F);
                for (int y = 0; y < 192; y++) {
                    for (int x = 0; x < 240; x++) {
                        int tx = x / 6;
                        if (tx >= 40) continue;
                        int charCode = vram[(nameTable + (y / 8) * 40 + tx) & vramMask];
                        UInt8 pattern = vram[(patternBase + charCode * 8 + (y % 8)) & vramMask];
                        if ((pattern >> (7 - (x % 6))) & 1) dst[(y + 24) * 272 + (x + 16)] = palette[fgCol];
                        else dst[(y + 24) * 272 + (x + 16)] = palette[bgColIdx];
                    }
                }
            } else if (mode == 1) {
                // Screen 1 (Text/Graphics Mode 32x24)
                int nameTable = (regs[2] << 10) & vramMask;
                int patternBase = (regs[4] << 11) & vramMask;
                int colorTable = (regs[3] << 6) & vramMask;
                for (int y = 0; y < 192; y++) {
                    for (int x = 0; x < 256; x++) {
                        int tx = x / 8;
                        int charCode = vram[(nameTable + (y / 8) * 32 + tx) & vramMask];
                        UInt8 pattern = vram[(patternBase + charCode * 8 + (y % 8)) & vramMask];
                        UInt8 color = vram[(colorTable + charCode / 8) & vramMask];
                        int bit = (pattern >> (7 - (x % 8))) & 1;
                        int colIdx = bit ? (color >> 4) : (color & 0x0F);
                        if (colIdx == 0) colIdx = bgCol;
                        dst[(y + 24) * 272 + (x + 8)] = palette[colIdx];
                    }
                }
            } else {
                // Screen 2 (Graphics Mode)
                int nameTable = (regs[2] << 10) & 0x3C00;
                int patternBase = (regs[4] & 0x3C) << 11;
                int patternMask = ((regs[4] & 0x03) << 11) | 0x7FF;
                int colorBase = (regs[3] & 0x80) << 6;
                int colorMask = ((regs[3] & 0x7F) << 6) | 0x3F;
                for (int y = 0; y < 192; y++) {
                    int zone = y / 64;
                    int py = y % 8;
                    for (int x = 0; x < 256; x++) {
                        int charCode = vram[(nameTable + (y / 8) * 32 + (x / 8)) & vramMask];
                        int index = (zone << 11) | (charCode << 3) | py;
                        UInt8 pattern = vram[(patternBase | (index & patternMask)) & vramMask];
                        UInt8 color   = vram[(colorBase   | (index & colorMask))   & vramMask];
                        int bit = (pattern >> (7 - (x % 8))) & 1;
                        int colIdx = bit ? (color >> 4) : (color & 0x0F);
                        if (colIdx == 0) colIdx = bgCol;
                        dst[(y + 24) * 272 + (x + 8)] = palette[colIdx];
                    }
                }
            }

            // Sprites (All Modes)
            int largeSprites = regs[1] & 0x02;
            int magnifiedSprites = regs[1] & 0x01;
            int spriteTab = ((int)regs[5] << 7) & vramMask;
            int spriteGen = ((int)regs[6] << 11) & vramMask;
            int displaySize = magnifiedSprites ? (largeSprites ? 32 : 16) : (largeSprites ? 16 : 8);

            for (int i = 0; i < 32; i++) {
                int sy_raw = vram[spriteTab + i * 4];
                if (sy_raw == 208) break;
                int sx = vram[spriteTab + i * 4 + 1];
                int si = vram[spriteTab + i * 4 + 2];
                int sc_raw = vram[spriteTab + i * 4 + 3];
                int sc = sc_raw & 0x0F;
                if (sc_raw & 0x80) sx -= 32;
                int sy = sy_raw;
                if (sy > 208) sy -= 256;
                sy++;
                if (sc) {
                    if (largeSprites) si &= ~0x03;
                    for (int py = 0; py < displaySize; py++) {
                        int vY = sy + py;
                        if (vY < 0 || vY >= 192) continue;
                        int vPy = magnifiedSprites ? (py / 2) : py;
                        for (int px = 0; px < displaySize; px++) {
                            int vX = sx + px;
                            if (vX < 0 || vX >= 256) continue;
                            int vPx = magnifiedSprites ? (px / 2) : px;
                            int tileOffset = largeSprites ? ((vPx / 8) * 2 + (vPy / 8)) : 0;
                            UInt8 pattern = vram[(spriteGen + (si + tileOffset) * 8 + (vPy % 8)) & 0x3FFF];
                            if ((pattern >> (7 - (vPx % 8))) & 1) dst[(vY + 24) * 272 + (vX + 8)] = palette[sc];
                        }
                    }
                }
            }
        }
        SDL_UnlockTexture(texture);
    }

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

extern "C" void saveScreenshot(const char* filename) {
    if (!renderer) return;
    SDL_Surface* s = SDL_CreateRGBSurface(0, 272 * 2, 240 * 2, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, filename);
    SDL_FreeSurface(s);
    if (debugMode) printf("Screenshot saved to %s\n", filename);
}

void initVideo() {
    window = SDL_CreateWindow("msxplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 272*2, 240*2, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 272, 240);

    if (vramViewerMode) {
        vramWindow = SDL_CreateWindow("VRAM Viewer", 0, 0, 512, 512, SDL_WINDOW_SHOWN);
        vramRenderer = SDL_CreateRenderer(vramWindow, -1, SDL_RENDERER_ACCELERATED);
        vramTexture = SDL_CreateTexture(vramRenderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 256, 256);
    }
}
