/**
 * Standalone viewer: loads .vram snapshots (see vram_snapshot.h) and draws with the same
 * raster path as msxplay (msx1RenderFrameToRgb565).
 */
#include "msx1_render_frame.h"
#include "vram_snapshot.h"
#include <SDL2/SDL.h>
#include <stdio.h>

static const int kFbW = 272, kFbH = 240;
static const float kAspect = 4.0f / 3.0f;

static void letterbox(int winW, int winH, int* vx, int* vy, int* vw, int* vh) {
    int vw0 = winW, vh0 = winH, vx0 = 0, vy0 = 0;
    if (winH > 0 && (float)winW / (float)winH > kAspect) {
        vw0 = (int)((float)winH * kAspect);
        vx0 = (winW - vw0) / 2;
    } else if (winW > 0) {
        vh0 = (int)((float)winW / kAspect);
        vy0 = (winH - vh0) / 2;
    }
    if (vw0 < 1) vw0 = 1;
    if (vh0 < 1) vh0 = 1;
    *vx = vx0;
    *vy = vy0;
    *vw = vw0;
    *vh = vh0;
}

static void rgb565ToArgb8888(const UInt16* src, Uint32* dst, int n) {
    for (int i = 0; i < n; i++) {
        UInt16 p = src[i];
        unsigned r = (unsigned)(p >> 11) & 31u, g = (unsigned)(p >> 5) & 63u, b = (unsigned)p & 31u;
        unsigned R = (r * 255u + 15u) / 31u;
        unsigned G = (g * 255u + 31u) / 63u;
        unsigned B = (b * 255u + 15u) / 31u;
        dst[i] = (Uint32)(0xff000000u | (R << 16) | (G << 8) | B);
    }
}

static int loadAndRender(const char* path, UInt8* vram, UInt8* regs, UInt16* palette, int* display_on, int* screen_mode,
                         UInt16* fb, Uint32* rgba, char* err, size_t errLen) {
    int ver = 0;
    if (!vramSnapshotReadFile(path, vram, regs, palette, display_on, screen_mode, &ver)) {
        snprintf(err, errLen, "cannot read or bad format: %s", path);
        return 0;
    }
    msx1RenderFrameToRgb565(vram, regs, palette, *display_on, *screen_mode, fb);
    rgb565ToArgb8888(fb, rgba, kFbW * kFbH);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: vramviewer <file.vram>\n");
        return 1;
    }
    const char* path = argv[1];

    UInt8 vram[VRAM_SNAPSHOT_VRAM_SIZE];
    UInt8 regs[VRAM_SNAPSHOT_REG_BYTES];
    UInt16 palette[16];
    int display_on = 0, screen_mode = 0;
    UInt16 fb[kFbW * kFbH];
    Uint32 rgba[kFbW * kFbH];

    char err[256];
    if (!loadAndRender(path, vram, regs, palette, &display_on, &screen_mode, fb, rgba, err, sizeof(err))) {
        fprintf(stderr, "vramviewer: %s\n", err);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "vramviewer: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* win =
        SDL_CreateWindow("vramviewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 544, 480, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "vramviewer: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, 0);
    }
    if (!ren) {
        fprintf(stderr, "vramviewer: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kFbW, kFbH);
    if (!tex) {
        fprintf(stderr, "vramviewer: SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    auto upload = [&]() {
        SDL_UpdateTexture(tex, NULL, rgba, kFbW * (int)sizeof(Uint32));
    };
    upload();

    char title[512];
    snprintf(title, sizeof(title), "vramviewer — %s  mode=%d disp=%d", path, screen_mode, display_on);
    SDL_SetWindowTitle(win, title);

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE)
                    running = 0;
                if (e.key.keysym.sym == SDLK_r) {
                    if (loadAndRender(path, vram, regs, palette, &display_on, &screen_mode, fb, rgba, err, sizeof(err))) {
                        upload();
                        snprintf(title, sizeof(title), "vramviewer — %s  mode=%d disp=%d", path, screen_mode, display_on);
                        SDL_SetWindowTitle(win, title);
                    } else
                        fprintf(stderr, "vramviewer: reload: %s\n", err);
                }
            }
        }
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        int vx, vy, vw, vh;
        letterbox(ww, wh, &vx, &vy, &vw, &vh);
        SDL_Rect dst = { vx, vy, vw, vh };
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
