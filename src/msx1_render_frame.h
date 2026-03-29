#ifndef MSX1_RENDER_FRAME_H
#define MSX1_RENDER_FRAME_H

#include "MsxTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Rasterize one MSX1 VDP frame into 272×240 RGB565 (same layout as msxplay RefreshScreen).
 * out_fb_272x240 must hold 272*240 UInt16.
 */
void msx1RenderFrameToRgb565(const UInt8* vram, const UInt8* regs, const UInt16* palette, int display_on,
                             int screen_mode, UInt16* out_fb_272x240);

#ifdef __cplusplus
}
#endif

#endif
