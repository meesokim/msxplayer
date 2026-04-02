
#include "msxplay.h"
#include "hash_util.h"
#include <stdio.h>

typedef struct {
    IoPortRead read;
    IoPortWrite write;
    void* ref;
} IoHandler;

static IoHandler ioHandlers[256];
static FILE* g_vdpTraceFile = NULL;

void vdpTraceOpen() {
    vdpTraceClose();
    g_vdpTraceFile = fopen("vdp_trace.log", "w");
}

void vdpTraceClose() {
    if (g_vdpTraceFile) {
        fclose(g_vdpTraceFile);
        g_vdpTraceFile = NULL;
    }
}

static void vdpTraceWrite(UInt16 port, UInt8 value) {
    if (ioHandlers[port].write) {
        ioHandlers[port].write(ioHandlers[port].ref, port, value);
    }
    if (g_vdpTraceFile) {
        UInt8* vram = vdpGetVramPtr();
        std::string h = sha1Hex(vram, 0x4000);
        fprintf(g_vdpTraceFile, "%02X %02X %s\n", (unsigned)(port & 0xFF), (unsigned)value, h.c_str());
        // Do not flush every time to keep performance; startEmulator and close will handle it.
    }
}

extern "C" void ioPortRegister(int port, IoPortRead read, IoPortWrite write, void* ref) {
    if (port >= 0 && port < 256) {
        if (debugMode) {
            printf("ioPortRegister: Port 0x%02X registered\n", port);
            fflush(stdout);
        }
        ioHandlers[port].read = read;
        ioHandlers[port].write = write;
        ioHandlers[port].ref = ref;
    }
}

extern "C" {

UInt8 readIoPort(void* ref, UInt16 port) {
    port &= 0xFF;
    if (port == 0xA8) return primarySlot;
    if (ioHandlers[port].read) {
        return ioHandlers[port].read(ioHandlers[port].ref, port);
    }
    return 0xFF;
}

int totalVramWrites = 0;

void writeIoPort(void* ref, UInt16 port, UInt8 value) {
    port &= 0xFF;
    if (g_isErrorGame && port >= 0x98 && port <= 0x9B) {
        vdpTraceWrite(port, value);
        return;
    }

    if (port == 0x98) {
        static int vramWriteCount = 0;
        if (debugMode && vramWriteCount++ % 1000 == 0) printf("VRAM Write: val=0x%02X, count=%d\n", value, vramWriteCount);
        else vramWriteCount++;
        totalVramWrites++;
    }

    if (port == 0xA8) {
        primarySlot = value;
        return;
    }
    if (ioHandlers[port].write) {
        ioHandlers[port].write(ioHandlers[port].ref, port, value);
    }
}

}

extern "C" void ioPortRegisterUnused(int idx, IoPortRead read, IoPortWrite write, void* ref) {}
extern "C" void ioPortUnregister(int port) {}
extern "C" void ioPortUnregisterUnused(int idx) {}
extern "C" void* ioPortGetRef(int port) { return ioHandlers[port & 0xFF].ref; }
