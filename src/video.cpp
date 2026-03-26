#include "msxplay.h"
#include "FrameBuffer.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>

// GL function pointers
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC) (void);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (APIENTRY *PFNGLGENBUFFERSPROC) (GLsizei n, GLuint* buffers);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC) (GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar* name);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (APIENTRY *PFNGLBINDATTRIBLOCATIONPROC) (GLuint program, GLuint index, const GLchar* name);

static PFNGLCREATESHADERPROC _glCreateShader;
static PFNGLSHADERSOURCEPROC _glShaderSource;
static PFNGLCOMPILESHADERPROC _glCompileShader;
static PFNGLCREATEPROGRAMPROC _glCreateProgram;
static PFNGLATTACHSHADERPROC _glAttachShader;
static PFNGLLINKPROGRAMPROC _glLinkProgram;
static PFNGLUSEPROGRAMPROC _glUseProgram;
static PFNGLGENBUFFERSPROC _glGenBuffers;
static PFNGLBINDBUFFERPROC _glBindBuffer;
static PFNGLBUFFERDATAPROC _glBufferData;
static PFNGLENABLEVERTEXATTRIBARRAYPROC _glEnableVertexAttribArray;
static PFNGLVERTEXATTRIBPOINTERPROC _glVertexAttribPointer;
static PFNGLGETUNIFORMLOCATIONPROC _glGetUniformLocation;
static PFNGLUNIFORM1FPROC _glUniform1f;
static PFNGLBINDATTRIBLOCATIONPROC _glBindAttribLocation;

static SDL_Window* window = NULL;
static SDL_GLContext glContext = NULL;
static GLuint shaderProgram = 0;
static GLuint textureId = 0;
static GLuint vbo = 0;
static GLint u_scanline_loc = -1;

extern "C" SDL_Window* getMainWindow() { return window; }

static const char* vertexShaderSource = 
    "attribute vec2 a_pos; attribute vec2 a_tex; varying vec2 v_tex;"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_tex = a_tex; }";

static const char* fragmentShaderSource = 
    "precision mediump float; varying vec2 v_tex; uniform sampler2D u_tex; uniform float u_scanline;"
    "void main() {"
    "    vec4 col = texture2D(u_tex, v_tex);"
    "    if (u_scanline > 0.5) {"
    "        float s = sin(v_tex.y * 240.0 * 3.141592) * 0.12 + 0.88;"
    "        col.rgb *= s;"
    "    }"
    "    gl_FragColor = col;"
    "}";

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = _glCreateShader(type);
    _glShaderSource(shader, 1, &source, NULL);
    _glCompileShader(shader);
    return shader;
}

extern "C" void* ioPortGetRef(int port);
extern "C" void vdpForceSync();
extern "C" UInt8* vdpGetVramPtr();
extern "C" UInt16* vdpGetPalettePtr();
extern "C" int vdpGetScreenOn();
extern "C" int vdpGetScreenMode();
extern "C" UInt8* vdpGetRegsPtr();
extern UInt8 bios[0x8000];

static UInt16 frameBuffer[272 * 240];

/* Register mix key: same as blueberryMSX updateScreenMode() first switch (MSX1). */
static int msx1RegMixKey(UInt8* regs) {
    return ((regs[0] & 0x0e) >> 1) | (regs[1] & 0x18);
}

/*
 * SCREEN 0 + 2 (mix key 0x11): VDP reports screenMode 0 but hardware uses
 * RefreshLine0Plus — text layout (40 cols / 256px) + Graphics II pattern fetch.
 * Drawing pure SCREEN 0 here causes wrong VRAM layout and vertical stripe artifacts.
 * Ported from blueberryMSX Common.h RefreshLine0Plus (hScroll=0).
 */
static void renderMSX1Screen0Plus(UInt8* vram, UInt8* regs, UInt16 pfg, UInt16 pbg, int vramMask) {
    const int chrTabBase = (((int)regs[2] << 10) | 0x3FF) & vramMask;
    const int chrGenBase = (((int)regs[4] << 11) | 0x7FF) & vramMask;
    const int ntOrMask = (int)(0xFFFFF000u); /* (-1<<12) */

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
                    frameBuffer[dstOff++] = pbg;
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
                    frameBuffer[dstOff++] = bit ? pfg : pbg;
                    bit = (patternByte >> (--shift)) & 1;
                    frameBuffer[dstOff++] = bit ? pfg : pbg;
                }
            }
        }
    }
}

/*
 * SCREEN 3 (multicolor): vdp screenMode == 3. Was incorrectly drawn with the Screen 2 path
 * (wrong VRAM layout → black or garbage). Ported from blueberryMSX Common.h RefreshLine3 (no sprites in cell).
 */
static void renderMSX1Screen3(UInt8* vram, UInt8* regs, UInt16* palette, int vramMask) {
    const int chrTabBase = (((int)regs[2] << 10) | 0x3FF) & vramMask;
    const int chrGenBase = (((int)regs[4] << 11) | 0x7FF) & vramMask;
    const int ntRowMask = (int)(0xFFFFFC00u);   /* (-1<<10) */
    const int patLineMask = (int)(0xFFFFF800u); /* (-1<<11) */

    for (int y = 0; y < 192; y++) {
        const int ntRow = (chrTabBase & (ntRowMask | (32 * (y / 8)))) & vramMask;
        const int patternBase = (chrGenBase & (patLineMask | ((y >> 2) & 7))) & vramMask;
        int dstOff = (y + 24) * 272 + 8;
        for (int tx = 0; tx < 32; tx++) {
            const UInt8 code = vram[(ntRow + tx) & vramMask];
            const UInt8 colPat = vram[(patternBase | (code * 8)) & vramMask];
            const UInt16 fc = palette[colPat >> 4];
            const UInt16 bc = palette[colPat & 0x0F];
            for (int p = 0; p < 4; p++) frameBuffer[dstOff++] = fc;
            for (int p = 0; p < 4; p++) frameBuffer[dstOff++] = bc;
        }
    }
}

// Basic font renderer using BIOS font
static void drawChar(int x, int y, char c, UInt16 fg, UInt16 bg, int w) {
    if (x < 0 || x > 272-w || y < 0 || y > 232) return;
    const UInt8* font = bios + 0x1BBF; // Standard MSX BIOS font location
    for (int i = 0; i < 8; i++) {
        UInt8 pat = font[(UInt8)c * 8 + i];
        for (int b = 0; b < w; b++) {
            frameBuffer[(y + i) * 272 + (x + b)] = (pat & (0x80 >> (b * 8 / w))) ? fg : bg;
        }
    }
}

static void drawText(int x, int y, const char* txt, UInt16 fg, UInt16 bg, int w) {
    while (*txt) {
        drawChar(x, y, *txt++, fg, bg, w);
        x += w;
    }
}

extern "C" void DrawMenu(const std::vector<std::string>& files, int selected, int offset, int biosMode, int fontEjk) {
    if (!window) return;
    memset(frameBuffer, 0, sizeof(frameBuffer));

    drawText(8, 4, "[MSXPLAYER ROM SELECT]", 0xFFFF, 0, 8);

    // Page counter
    int pageSize = 24;
    int totalPages = (files.size() + pageSize - 1) / pageSize;
    int curPage = (offset / pageSize) + 1;
    char pageBuf[16];
    sprintf(pageBuf, "%d/%d", curPage, totalPages);
    drawText(220, 4, pageBuf, 0xFFE0, 0, 8); // Yellow page counter

    const int nfiles = (int)files.size();
    for (int i = 0; i < pageSize && offset + i < nfiles; i++) {
        int idx = offset + i;
        bool isSel = (idx == selected);
        UInt16 fg = isSel ? 0x0000 : 0xFFFF;
        UInt16 bg = isSel ? 0xF800 : 0x0000;

        char buf[41];
        strncpy(buf, files[idx].c_str(), 40); buf[40] = 0;

        if (isSel) {
            for (int ry = 0; ry < 8; ry++) {
                for (int rx = 0; rx < 272; rx++) {
                    frameBuffer[(16 + i * 8 + ry) * 272 + rx] = bg;
                }
            }
        }

        // drawText(8, 16 + i * 8, (idx == selected) ? ">" : " ", fg, bg, 6);
        drawText(8, 16 + i * 8, buf, fg, bg, 6);
    }

    {
        char foot1[52];
        {
            const char* bl = "emb";
            if (biosMode == 1) bl = "C-BIOS+basic";
            else if (biosMode == 2) bl = "VG8020";
            else if (biosMode == 3) bl = "main+logo";
            snprintf(foot1, sizeof(foot1), "B=BIOS:%s", bl);
        }
        drawText(8, 208, foot1, 0xFFE0, 0, 6);
        const char* act = (fontEjk == 1) ? "J" : ((fontEjk == 2) ? "K" : "E");
        char foot2[52];
        snprintf(foot2, sizeof(foot2), "E/J/K font (one): %s", act);
        drawText(8, 216, foot2, 0xAD55, 0, 6);
    }

    int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 272, 240, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, frameBuffer);
    _glUseProgram(shaderProgram);
    _glUniform1f(u_scanline_loc, 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(window);
}

extern "C" void RefreshScreen(int screenMode) {
    if (!window) return;
    UInt8* vram = vdpGetVramPtr();
    UInt16* palette = vdpGetPalettePtr();
    UInt8* regs = vdpGetRegsPtr();
    if (!vram || !palette || !regs) return;

    int bgCol = regs[7] & 0x0F;
    UInt16 bgPixel = palette[bgCol];
    int displayEnabled = vdpGetScreenOn();
    int mode = vdpGetScreenMode();
    int vramMask = 0x3FFF;

    for (int i = 0; i < 272 * 240; i++) frameBuffer[i] = bgPixel;

    if (displayEnabled) {
        const int mixKey = msx1RegMixKey(regs);
        int fg = regs[7] >> 4, bg = regs[7] & 0x0F;
        UInt16 pfg = palette[fg], pbg = palette[bg];

        if (mode == 0 && mixKey == 0x11) {
            /* SCREEN 0 + Graphics II (e.g. Alpha Roid title) */
            renderMSX1Screen0Plus(vram, regs, pfg, pbg, vramMask);
        } else if (mode == 0) { // Screen 0 only (mixKey 0x10)
            int nt = (regs[2] << 10) & vramMask, pb = (regs[4] << 11) & vramMask;
            for (int y = 0; y < 192; y++) {
                int py = y % 8, row = (y / 8) * 40, dstOff = (y + 24) * 272 + 16;
                for (int tx = 0; tx < 40; tx++) {
                    UInt8 pat = vram[(pb + vram[(nt + row + tx) & vramMask] * 8 + py) & vramMask];
                    for (int b = 0; b < 6; b++) frameBuffer[dstOff + tx*6 + b] = (pat & (0x80 >> b)) ? pfg : pbg;
                }
            }
        } else if (mode == 1) { // Screen 1
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
                        frameBuffer[dstOff + tx * 8 + b] = (pat & (0x80 >> b)) ? pfg : pbg;
                    }
                }
            }
        } else if (mode == 3) { // Screen 3 multicolor (e.g. Bifamu)
            renderMSX1Screen3(vram, regs, palette, vramMask);
        } else { // Screen 2
            int nt = (regs[2] << 10) & 0x3C00, pb = (regs[4] & 0x3C) << 11, pm = ((regs[4] & 0x03) << 11) | 0x7FF;
            int cb = (regs[3] & 0x80) << 6, cm = ((regs[3] & 0x7F) << 6) | 0x3F;
            for (int y = 0; y < 192; y++) {
                int zone = (y / 64) << 11, py = y % 8, row = (y / 8) * 32, dstOff = (y + 24) * 272 + 8;
                for (int tx = 0; tx < 32; tx++) {
                    int idx = zone | (vram[(nt + row + tx) & vramMask] << 3) | py;
                    UInt8 pat = vram[(pb | (idx & pm)) & vramMask], col = vram[(cb | (idx & cm)) & vramMask];
                    UInt16 pfg = palette[col >> 4], pbg = palette[col & 0x0F];
                    for (int b = 0; b < 8; b++) frameBuffer[dstOff + tx*8 + b] = (pat & (0x80 >> b)) ? pfg : pbg;
                }
            }
        }
        // Optimized Sprites
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
            int sc_raw = vram[spriteTab + i * 4 + 3]; int sc = sc_raw & 0x0F; if (!sc) continue;
            int sx = vram[spriteTab + i * 4 + 1]; if (sc_raw & 0x80) sx -= 32;
            int si = vram[spriteTab + i * 4 + 2]; if (large) si &= ~0x03;
            int sy = (sy_raw > 208) ? sy_raw - 255 : sy_raw + 1;
            UInt16 psc = palette[sc];
            for (int py = 0; py < size; py++) {
                int vY = sy + py; if (vY < 0 || vY >= 192) continue;
                int rowOff = (vY + 24) * 272, vPy = mag ? (py / 2) : py;
                for (int px = 0; px < size; px++) {
                    int vX = sx + px; if (vX < 0 || vX >= 256) continue;
                    int vPx = mag ? (px / 2) : px;
                    int tOff = large ? ((vPx / 8) * 2 + (vPy / 8)) : 0;
                    if ((vram[(spriteGen + (si + tOff) * 8 + (vPy % 8)) & 0x3FFF] >> (7 - (vPx % 8))) & 1) 
                        frameBuffer[rowOff + (vX + 8)] = psc;
                }
            }
        }
    }

    int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
    float targetAspect = 272.0f / 240.0f;
    int vpW = winW, vpH = winH, vpX = 0, vpY = 0;
    if ((float)winW / winH > targetAspect) { vpW = (int)(winH * targetAspect); vpX = (winW - vpW) / 2; }
    else { vpH = (int)(winW / targetAspect); vpY = (winH - vpH) / 2; }

    glViewport(vpX, vpY, vpW, vpH);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 272, 240, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, frameBuffer);
    _glUseProgram(shaderProgram);
    _glUniform1f(u_scanline_loc, (scanlinesEnabled && winH > 240) ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(window);
}

extern "C" void saveScreenshot(const char* filename) {
    SDL_Surface* s = SDL_CreateRGBSurface(0, 272, 240, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    glReadPixels(0, 0, 272, 240, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
    SDL_SaveBMP(s, filename);
    SDL_FreeSurface(s);
}

#define LOAD_GL(name, type) _##name = (type)SDL_GL_GetProcAddress(#name);

void initVideo() {
    window = SDL_CreateWindow("msxplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 272*2, 240*2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    glContext = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(0);

    LOAD_GL(glCreateShader, PFNGLCREATESHADERPROC);
    LOAD_GL(glShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD_GL(glCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD_GL(glCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD_GL(glAttachShader, PFNGLATTACHSHADERPROC);
    LOAD_GL(glLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD_GL(glUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD_GL(glGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD_GL(glBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD_GL(glBufferData, PFNGLBUFFERDATAPROC);
    LOAD_GL(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD_GL(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD_GL(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD_GL(glUniform1f, PFNGLUNIFORM1FPROC);
    LOAD_GL(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC);

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    shaderProgram = _glCreateProgram();
    _glAttachShader(shaderProgram, vs);
    _glAttachShader(shaderProgram, fs);
    _glBindAttribLocation(shaderProgram, 0, "a_pos");
    _glBindAttribLocation(shaderProgram, 1, "a_tex");
    _glLinkProgram(shaderProgram);
    u_scanline_loc = _glGetUniformLocation(shaderProgram, "u_scanline");

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 272, 240, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    float verts[] = { -1,1,0,0, -1,-1,0,1, 1,1,1,0, 1,-1,1,1 };
    _glGenBuffers(1, &vbo);
    _glBindBuffer(GL_ARRAY_BUFFER, vbo);
    _glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    _glEnableVertexAttribArray(0);
    _glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    _glEnableVertexAttribArray(1);
    _glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}
