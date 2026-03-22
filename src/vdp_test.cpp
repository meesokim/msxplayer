#include "msxplay.h"
#include <stdio.h>
#include <string.h>

extern "C" void vdpUpdateRegisters(void* vdp, UInt8 reg, UInt8 value);
extern "C" FrameBuffer* frameBufferGetDrawFrame();

void runVdpDiagnostics() {
    void* vdp = ioPortGetRef(0x99);
    if (!vdp) { printf("VDP not initialized!\n"); return; }

    printf("\n=== Starting VDP Screen Out Diagnostics ===\n");

    // 1. Register Access Test
    vdpUpdateRegisters(vdp, 7, 0xF4); // Text: White, BG: Dark Blue
    printf("[1] Register Write Test: Done\n");

    // 2. VRAM Pattern Test (Screen 2)
    printf("[2] Testing Screen 2 Pattern Mapping...\n");
    vdpUpdateRegisters(vdp, 0, 0x02); // Mode 2
    vdpUpdateRegisters(vdp, 1, 0x40); // Display ON
    
    // Set VRAM address 0x0000 for writing
    writeIoPort(vdp, 0x99, 0x00);
    writeIoPort(vdp, 0x99, 0x40); 
    
    for (int i = 0; i < 0x4000; i++) writeIoPort(vdp, 0x98, 0);
    
    // Set Name Table (0x1800)
    writeIoPort(vdp, 0x99, 0x00);
    writeIoPort(vdp, 0x99, 0x40 | 0x18);
    for (int i = 0; i < 768; i++) writeIoPort(vdp, 0x98, i & 0xFF);
    
    printf("    VRAM Test Patterns Injected.\n");

    // 3. Sprite System Test
    printf("[3] Testing Sprite System...\n");
    vdpUpdateRegisters(vdp, 1, 0x42); // 16x16 sprites
    vdpUpdateRegisters(vdp, 5, 0x38); // Sprite Attr Table at 0x1C00
    
    // Sprite 0: At (100, 100)
    writeIoPort(vdp, 0x99, 0x00);
    writeIoPort(vdp, 0x99, 0x40 | 0x1C);
    writeIoPort(vdp, 0x98, 100);
    writeIoPort(vdp, 0x98, 100);
    writeIoPort(vdp, 0x98, 0);
    writeIoPort(vdp, 0x98, 0x0F);

    // 4. FrameBuffer Integrity Check
    RefreshScreen(2);
    FrameBuffer* fb = frameBufferGetDrawFrame();
    if (fb) {
        int nonZero = 0;
        for (int i = 0; i < 640 * 240; i++) if (fb->fb[i] != 0) nonZero++;
        printf("[4] FrameBuffer Check: %d pixels rendered.\n", nonZero);
    }

    printf("=== VDP Diagnostics Complete ===\n\n");
}
