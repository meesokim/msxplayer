
#include "msxplay.h"
#include <stdio.h>

typedef struct {
    IoPortRead read;
    IoPortWrite write;
    void* ref;
} IoHandler;

static IoHandler ioHandlers[256];

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
