#include "msxplay.h"
#include "FrameBuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_ttf.h>

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
/** Full viewport resolution; ROM menu draws natively here (no 272×240 upscale). */
static GLuint menuTextureId = 0;
static int menuTextureAllocW = 0;
static int menuTextureAllocH = 0;
static GLuint vbo = 0;
static GLint u_scanline_loc = -1;

extern "C" SDL_Window* getMainWindow() { return window; }

static const char* vertexShaderSource = 
    "attribute vec2 a_pos; attribute vec2 a_tex; varying vec2 v_tex;"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_tex = a_tex; }";

/* CRT-style: per-emulated-line mask (240 lines), soft falloff, slight vertical bleed, vignette */
static const char* fragmentShaderSource = R"GLSL(
precision mediump float;
varying vec2 v_tex;
uniform sampler2D u_tex;
uniform float u_scanline;
void main() {
    vec4 col = texture2D(u_tex, v_tex);
    if (u_scanline > 0.5) {
        float ph = fract(v_tex.y * 240.0);
        float beam = smoothstep(0.0, 0.19, ph) * smoothstep(1.0, 0.73, ph);
        float shade = mix(0.71, 1.0, beam);
        vec3 lit = col.rgb * shade;
        vec2 belowUV = clamp(v_tex + vec2(0.0, 0.55 / 240.0), vec2(0.0), vec2(1.0));
        vec3 tail = texture2D(u_tex, belowUV).rgb;
        float bleed = (1.0 - beam) * 0.14;
        col.rgb = mix(lit, tail * 0.93, bleed);
        vec2 c = (v_tex - 0.5) * 1.12;
        float vig = 0.88 + 0.12 * smoothstep(1.05, 0.2, dot(c, c));
        col.rgb *= vig;
        col.rgb = clamp(col.rgb, 0.0, 1.0);
    }
    gl_FragColor = col;
}
)GLSL";

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

/* ROM menu palette (RGB565) */
static const UInt16 kMenuBg = 0x0841;
static const UInt16 kMenuTitle = 0xFEC0;
static const UInt16 kMenuTitShadow = 0x18C3;
static const UInt16 kMenuRowFg = 0xDEDB;
static const UInt16 kMenuSelBg = 0x2D7F;
static const UInt16 kMenuSelTop = 0x4E9F;
static const UInt16 kMenuSelFg = 0xFFFF;
static const UInt16 kMenuFootHi = 0xFEC0;
static const UInt16 kMenuFootLo = 0x8C72;

static UInt16 menuBlend565(UInt16 fg, UInt16 bg, unsigned wFg256) {
    unsigned ar = (fg >> 11) & 0x1Fu, ag = (fg >> 5) & 0x3Fu, ab = fg & 0x1Fu;
    unsigned br = (bg >> 11) & 0x1Fu, bbg = (bg >> 5) & 0x3Fu, bb = bg & 0x1Fu;
    unsigned dr = (ar * wFg256 + br * (256u - wFg256)) >> 8;
    unsigned dg = (ag * wFg256 + bbg * (256u - wFg256)) >> 8;
    unsigned db = (ab * wFg256 + bb * (256u - wFg256)) >> 8;
    if (dr > 31) dr = 31;
    if (dg > 63) dg = 63;
    if (db > 31) db = 31;
    return (UInt16)((dr << 11) | (dg << 5) | db);
}

static TTF_Font* g_menuTitleFont = nullptr;
static int g_menuTitleFontPx = 0;

static void menu565ToRgb888(UInt16 c, Uint8* r, Uint8* g, Uint8* b) {
    unsigned rr = (unsigned)(c >> 11) & 31u, gg = (unsigned)(c >> 5) & 63u, bb = (unsigned)c & 31u;
    *r = (Uint8)((rr * 255u + 15u) / 31u);
    *g = (Uint8)((gg * 255u + 31u) / 63u);
    *b = (Uint8)((bb * 255u + 15u) / 31u);
}

static UInt16 menuRgb888To565(Uint8 r, Uint8 g, Uint8 b) {
    unsigned rr = ((unsigned)r * 31u + 127u) / 255u;
    unsigned gg = ((unsigned)g * 63u + 127u) / 255u;
    unsigned bb = ((unsigned)b * 31u + 127u) / 255u;
    if (rr > 31u) rr = 31u;
    if (gg > 63u) gg = 63u;
    if (bb > 31u) bb = 31u;
    return (UInt16)((rr << 11) | (gg << 5) | bb);
}

static void menuBlitArgbSurfaceTo565(UInt16* fb, int fbW, int fbH, int dx, int dy, SDL_Surface* surf) {
    if (!surf) return;
    SDL_Surface* conv = nullptr;
    SDL_Surface* s = surf;
    if (surf->format->format != SDL_PIXELFORMAT_ARGB8888) {
        conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!conv) return;
        s = conv;
    }
    if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);
    int w = s->w, h = s->h;
    for (int y = 0; y < h; y++) {
        const Uint32* row = (const Uint32*)((const Uint8*)s->pixels + y * s->pitch);
        for (int x = 0; x < w; x++) {
            Uint32 p = row[x];
            Uint8 r, g, b, a;
            SDL_GetRGBA(p, s->format, &r, &g, &b, &a);
            int px = dx + x, py = dy + y;
            if (px < 0 || py < 0 || px >= fbW || py >= fbH) continue;
            UInt16* d = &fb[py * fbW + px];
            if (a == 0) continue;
            UInt16 fg = menuRgb888To565(r, g, b);
            if (a == 255) *d = fg;
            else *d = menuBlend565(fg, *d, (unsigned)a * 256u / 255u);
        }
    }
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    if (conv) SDL_FreeSurface(conv);
}

static bool menuEnsureTitleFont(int px) {
    if (px < 8) px = 8;
    if (g_menuTitleFont && g_menuTitleFontPx == px) return true;
    if (g_menuTitleFont) {
        TTF_CloseFont(g_menuTitleFont);
        g_menuTitleFont = nullptr;
        g_menuTitleFontPx = 0;
    }
    static std::string baseFont;
    char* base = SDL_GetBasePath();
    if (base) {
        baseFont = std::string(base) + "fonts/MenuTitle.ttf";
        SDL_free(base);
        g_menuTitleFont = TTF_OpenFont(baseFont.c_str(), px);
        if (g_menuTitleFont) {
            g_menuTitleFontPx = px;
            return true;
        }
    }
    static const char* kTryUnix[] = {
        "fonts/MenuTitle.ttf",
        "./fonts/MenuTitle.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Bold.ttf",
        nullptr
    };
    static const char* kWinRel[] = { "/Fonts/arialbd.ttf", "/Fonts/segoeuib.ttf", "/Fonts/calibrib.ttf", nullptr };
    const char* windir = getenv("SYSTEMROOT");
    if (!windir) windir = getenv("WINDIR");
    if (windir) {
        static std::string winFont;
        for (int i = 0; kWinRel[i]; i++) {
            winFont = std::string(windir) + kWinRel[i];
            g_menuTitleFont = TTF_OpenFont(winFont.c_str(), px);
            if (g_menuTitleFont) {
                g_menuTitleFontPx = px;
                return true;
            }
        }
    }
    for (int i = 0; kTryUnix[i]; i++) {
        g_menuTitleFont = TTF_OpenFont(kTryUnix[i], px);
        if (g_menuTitleFont) {
            g_menuTitleFontPx = px;
            return true;
        }
    }
    return false;
}

static bool menuDrawTitleTtf(UInt16* fb, int fbW, int fbH, int marginX, int marginY, int titlePx, const char* text) {
    if (!menuEnsureTitleFont(titlePx)) return false;
    Uint8 sr, sg, sb, mr, mg, mb;
    menu565ToRgb888(kMenuTitShadow, &sr, &sg, &sb);
    menu565ToRgb888(kMenuTitle, &mr, &mg, &mb);
    SDL_Color shadow = { sr, sg, sb, 255 };
    SDL_Color mainc = { mr, mg, mb, 255 };
    SDL_Surface* sh = TTF_RenderUTF8_Blended(g_menuTitleFont, text, shadow);
    SDL_Surface* ma = TTF_RenderUTF8_Blended(g_menuTitleFont, text, mainc);
    if (!ma) {
        if (sh) SDL_FreeSurface(sh);
        return false;
    }
    int off = std::max(1, titlePx / 14);
    if (sh) {
        menuBlitArgbSurfaceTo565(fb, fbW, fbH, marginX + off, marginY + off, sh);
        SDL_FreeSurface(sh);
    }
    menuBlitArgbSurfaceTo565(fb, fbW, fbH, marginX, marginY, ma);
    SDL_FreeSurface(ma);
    return true;
}

/** Scale MSX 8×8 font to cw×ch with nearest-neighbor (integer only — was 8× supersampled floats per pixel). */
static void menuDrawGlyph(UInt16* fb, int fbW, int fbH, int x, int y, unsigned char uc, UInt16 fg, UInt16 bg, int cw,
    int glyphH, const UInt8* ft) {
    if (uc < 32) uc = (unsigned char)' ';
    const UInt8* gbase = ft + (unsigned)uc * 8u;
    if (cw < 1 || glyphH < 1) return;
    for (int i = 0; i < glyphH; i++) {
        int py = y + i;
        if (py < 0 || py >= fbH) continue;
        int fontRow = (i * 8) / glyphH;
        if (fontRow > 7) fontRow = 7;
        UInt8 pat = gbase[fontRow];
        int rowOff = py * fbW;
        for (int b = 0; b < cw; b++) {
            int px = x + b;
            if (px < 0 || px >= fbW) continue;
            int fontCol = (b * 8) / cw;
            if (fontCol > 7) fontCol = 7;
            fb[rowOff + px] = (pat & (UInt8)(0x80u >> (unsigned)fontCol)) ? fg : bg;
        }
    }
}

static void menuDrawText(UInt16* fb, int fbW, int fbH, int x, int y, const char* txt, UInt16 fg, UInt16 bg, int cw,
    int glyphH, const UInt8* ft) {
    while (*txt) {
        unsigned char uc = (unsigned char)*txt++;
        menuDrawGlyph(fb, fbW, fbH, x, y, uc, fg, bg, cw, glyphH, ft);
        x += cw;
    }
}

/* 4:3 TV aspect: 256×192 active picture on a 4:3 CRT has ~4:3 dot centering; stretch vs square 272×240 buffer. */
static const float kMsxDisplayAspectRatio = 4.0f / 3.0f;

/** Letterbox to 4:3; 272×240 texture is scaled into this viewport (menu uses same helper). */
static void menuComputeLetterboxViewport(int winW, int winH, int* vpX, int* vpY, int* vpW, int* vpH) {
    float targetAspect = kMsxDisplayAspectRatio;
    int vw = winW, vh = winH, vx = 0, vy = 0;
    if (winH > 0 && (float)winW / (float)winH > targetAspect) {
        vw = (int)((float)winH * targetAspect);
        vx = (winW - vw) / 2;
    } else if (winW > 0) {
        vh = (int)((float)winW / targetAspect);
        vy = (winH - vh) / 2;
    }
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;
    *vpX = vx;
    *vpY = vy;
    *vpW = vw;
    *vpH = vh;
}

extern "C" void DrawMenu(const std::vector<std::string>& files, int selected, int offset, int biosMode) {
    if (!window) return;
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    int vpX, vpY, vpW, vpH;
    menuComputeLetterboxViewport(winW, winH, &vpX, &vpY, &vpW, &vpH);

    static std::vector<UInt16> menuFb;
    const size_t need = (size_t)vpW * (size_t)vpH;
    if (menuFb.size() != need) menuFb.resize(need);
    UInt16* fb = menuFb.data();
    std::fill(fb, fb + need, kMenuBg);

    const UInt8* ft = bios + 0x1BBF;
    const int pageSize = MSXPLAY_MENU_PAGE_SIZE;

    const int marginX = std::max(4, vpW / 48);
    const int marginY = std::max(2, vpH / 80);

    int titleCh = std::max(10, std::min(vpH / 11, vpH / 5));
    int titleCw = titleCh;

    int footCh = std::max(8, std::min(vpH / 24, titleCh));
    int footCw = std::max(5, (footCh * 7 + 9) / 10);
    const int footGap = std::max(2, marginY / 2);
    const int yFoot1 = vpH - marginY - 2 * footCh - footGap;
    const int yFoot2 = yFoot1 + footCh + footGap;

    int yList = marginY + titleCh + marginY;
    int listBudget = yFoot1 - marginY - yList;
    if (listBudget < pageSize * 6) {
        titleCh = std::max(10, titleCh - 6);
        titleCw = titleCh;
        yList = marginY + titleCh + marginY;
        listBudget = yFoot1 - marginY - yList;
    }
    int lineH = listBudget / pageSize;
    lineH = std::max(4, std::min(lineH, vpH / 4));
    if (yList + pageSize * lineH > yFoot1 - marginY)
        lineH = std::max(3, (yFoot1 - marginY - yList) / pageSize);
    int listCw = std::max(4, (lineH * 7 + 9) / 10);
    int listCh = lineH;

    const char* title = "[MSXPLAYER ROM SELECT]";
    if (!menuDrawTitleTtf(fb, vpW, vpH, marginX, marginY, titleCh, title)) {
        const int shadow = std::max(1, titleCh / 12);
        menuDrawText(fb, vpW, vpH, marginX + shadow, marginY + shadow, title, kMenuTitShadow, kMenuBg, titleCw, titleCh, ft);
        menuDrawText(fb, vpW, vpH, marginX, marginY, title, kMenuTitle, kMenuBg, titleCw, titleCh, ft);
    }

    int totalPages = (int)((files.size() + (size_t)pageSize - 1) / (size_t)pageSize);
    if (totalPages < 1) totalPages = 1;
    int curPage = (offset / pageSize) + 1;
    char pageBuf[24];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", curPage, totalPages);
    int pageStrW = (int)strlen(pageBuf) * footCw;
    int pyPage = marginY + std::max(0, (titleCh - footCh) / 2);
    menuDrawText(fb, vpW, vpH, vpW - marginX - pageStrW, pyPage, pageBuf, kMenuFootHi, kMenuBg, footCw, footCh, ft);

    const int nfiles = (int)files.size();
    int maxFilenameChars = (vpW - 2 * marginX - 8) / listCw;
    maxFilenameChars = std::max(8, std::min(maxFilenameChars, 80));

    for (int i = 0; i < pageSize && offset + i < nfiles; i++) {
        int idx = offset + i;
        bool isSel = (idx == selected);
        int rowY = yList + i * lineH;
        UInt16 fg = isSel ? kMenuSelFg : kMenuRowFg;
        UInt16 lineBg = isSel ? kMenuSelBg : kMenuBg;

        for (int ry = 0; ry < lineH; ry++) {
            UInt16 bar = lineBg;
            if (isSel) {
                if (ry == 0) bar = menuBlend565(kMenuSelTop, kMenuSelBg, 200);
                else if (ry == 1) bar = menuBlend565(kMenuSelTop, kMenuSelBg, 55);
            }
            int pyy = rowY + ry;
            if (pyy < 0 || pyy >= vpH) continue;
            for (int rx = 0; rx < vpW; rx++) fb[pyy * vpW + rx] = bar;
        }

        char buf[96];
        strncpy(buf, files[idx].c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        if ((int)strlen(buf) > maxFilenameChars) buf[maxFilenameChars] = 0;

        int textY = rowY + (lineH - listCh) / 2;
        if (textY < rowY) textY = rowY;
        menuDrawText(fb, vpW, vpH, marginX + 4, textY, buf, fg, lineBg, listCw, listCh, ft);
    }

    {
        char foot1[52];
        {
            const char* bl = "emb";
            if (biosMode == 1) bl = "C-BIOS";
            else if (biosMode == 2) bl = "VG8020";
            else if (biosMode == 3) bl = "main+logo";
            else if (biosMode == 4) bl = "HB-10";
            else if (biosMode == 5) bl = "C-BIOS JP";
            snprintf(foot1, sizeof(foot1), "B: next BIOS  now:%s", bl);
        }
        menuDrawText(fb, vpW, vpH, marginX, yFoot1, foot1, kMenuFootHi, kMenuBg, footCw, footCh, ft);
        menuDrawText(fb, vpW, vpH, marginX, yFoot2, "Enter=start  Esc=quit", kMenuFootLo, kMenuBg, footCw, footCh, ft);
    }

    if (menuTextureId == 0) {
        glGenTextures(1, &menuTextureId);
        glBindTexture(GL_TEXTURE_2D, menuTextureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    if (menuTextureAllocW != vpW || menuTextureAllocH != vpH) {
        glBindTexture(GL_TEXTURE_2D, menuTextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vpW, vpH, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
        menuTextureAllocW = vpW;
        menuTextureAllocH = vpH;
    }
    glViewport(vpX, vpY, vpW, vpH);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, menuTextureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vpW, vpH, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, fb);
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
        } else { // Screen 2 (Graphics II) — blueberryMSX RefreshLine2: index unmasked; only final addr & vramMask
            const int chrTabBase = (((int)regs[2] << 10) | 0x3FF) & vramMask;
            const int chrGenBase = (((int)regs[4] << 11) | 0x7FF) & vramMask;
            const int colTabBase = (((int)regs[3] << 6) | 0x3F) & vramMask;
            for (int y = 0; y < 192; y++) {
                const int row = (y / 8) * 32;
                /* Avoid (-1<<13) on signed (UB); same bits as blueberry base */
                const unsigned baseU = 0xFFFFE000u | ((unsigned)(y & 0xC0) << 5) | (unsigned)(y & 7);
                int dstOff = (y + 24) * 272 + 8;
                for (int tx = 0; tx < 32; tx++) {
                    const int ntAddr = (chrTabBase & (int)(0xFFFFFC00u | (unsigned)(row + tx))) & vramMask;
                    const UInt8 code = vram[ntAddr];
                    const int idx = (int)(baseU | ((unsigned)code << 3));
                    const UInt8 pat = vram[(chrGenBase & idx) & vramMask];
                    const UInt8 col = vram[(colTabBase & idx) & vramMask];
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

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    int vpX, vpY, vpW, vpH;
    menuComputeLetterboxViewport(winW, winH, &vpX, &vpY, &vpW, &vpH);

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
    window = SDL_CreateWindow("msxplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)(240 * 2 * kMsxDisplayAspectRatio), 240 * 2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    glContext = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(0);
    if (TTF_Init() == -1)
        fprintf(stderr, "msxplay: TTF_Init failed: %s\n", TTF_GetError());

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
