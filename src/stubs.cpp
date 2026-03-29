#include "msxplay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
    UInt32 currentBoardTime = 0;
    UInt32* boardSysTime = &currentBoardTime;
    R800* cpu = NULL;
}

// SaveState, Debug, Device mocks (Standard)
extern "C" int saveStateOpenForRead(const char* name) { return 0; }
extern "C" int saveStateOpenForWrite(const char* name) { return 0; }
extern "C" void saveStateClose() {}
extern "C" void saveStateGet(int id, void* data, int size) {}
extern "C" void saveStateSet(int id, void* data, int size) {}
extern "C" void saveStateGetBuffer(int id, void* data, int size) {}
extern "C" void saveStateSetBuffer(int id, void* data, int size) {}
extern "C" int debugDeviceRegister(int type, const char* name, void* callbacks, void* ref) { return 1; }
extern "C" void debugDeviceUnregister(int handle) {}
extern "C" void dbgDeviceAddMemoryBlock(void* ref, const char* name, void* read, void* write, void* ref2, int size) {}
extern "C" void dbgDeviceAddRegisterBank(void* ref, const char* name, void* write, void* ref2) {}
extern "C" void dbgRegisterBankAddRegister(void* bank, const char* name, int size, int value) {}
extern "C" void dbgDeviceAddIoPorts(void* ref, const char* name, int count, void* ref2) {}
extern "C" void dbgIoPortsAddPort(void* ports, int index, const char* name, int read, int write) {}
extern "C" int debuggerCheckVramAccess() { return 0; }
extern "C" void tryWatchpoint(int type, int address, UInt8 value, void* ref, void* cb) {}
extern "C" int deviceManagerRegister(int type, void* callbacks, void* ref) { return 1; }
extern "C" void deviceManagerUnregister(int handle) {}

// Board mocks
extern "C" int boardGetInt() { return 0; }
extern "C" int boardGetVideoAutodetect() { return 0; }
extern "C" void boardOnBreakpoint(int address) {}
extern "C" void boardClearInt(UInt32 mask) { 
    if (cpu) r800ClearInt(cpu); 
}
extern "C" void boardSetInt(UInt32 mask) { 
    static int lastSet = 0;
    if (debugMode && lastSet++ % 60 == 0) printf("boardSetInt mask=%u\n", mask);
    else lastSet++;
    if (cpu) r800SetInt(cpu); 
}

// THE BUFFER: No longer needed here as we use FrameBuffer from blueMSX
extern "C" UInt16* archVideoInBufferGet(int width, int height) { return NULL; }
extern "C" UInt16* getSharedVdpBuffer() { return NULL; }

// Language strings mocks
// ...
extern "C" const char* langDbgMemVram() { return "VRAM"; }
extern "C" const char* langDbgRegs() { return "Registers"; }
extern "C" const char* langDbgDevV9958() { return "V9958"; }
extern "C" const char* langDbgDevV9938() { return "V9938"; }
extern "C" const char* langDbgDevTms99x8A() { return "TMS99x8A"; }
extern "C" const char* langDbgDevTms9929A() { return "TMS9929A"; }
extern "C" const char* langDbgDevAy8910() { return "AY8910"; }

// VDP Cmd mocks
extern "C" void vdpCmdExecute(void* vdpCmdState) {}
extern "C" void vdpCmdLoadState(void* vdpCmdState) {}
extern "C" void vdpCmdSaveState(void* vdpCmdState) {}
extern "C" void vdpCmdDestroy(void* vdpCmdState) {}
extern "C" void vdpCmdPeek(void* vdpCmdState) {}
extern "C" void vdpCmdWrite(void* vdpCmdState, int reg, UInt8 value) {}
extern "C" void* vdpCmdCreate(int vramSize, UInt8* vramPtr, UInt32 systemTime) {
    (void)vramSize;
    (void)vramPtr;
    (void)systemTime;
    return (void*)1;
}

#include "VideoManager.h"
#include "FrameBuffer.h"

extern "C" void* archSemaphoreCreate(int count) { return (void*)1; }
extern "C" void archSemaphoreDestroy(void* sem) {}
extern "C" void archSemaphoreWait(void* sem, int timeout) {}
extern "C" void archSemaphoreSignal(void* sem) {}

extern "C" void frameBufferSetActive(FrameBufferData* frameBuffer);

extern "C" int videoManagerRegister(const char* name, FrameBufferData* frameBuffer, VideoCallbacks* callbacks, void* ref) { 
    if (frameBuffer) {
        frameBufferSetActive(frameBuffer);
    }
    if (callbacks && callbacks->enable) {
        callbacks->enable(ref);
    }
    return 1; 
}
extern "C" void videoManagerUnregister(int handle) {}
extern "C" int videoManagerGetCount() { return 1; }
extern "C" void videoManagerSetMode(int index, VideoMode mode, VideoMode mask) {}
extern "C" void videoManagerSetActive(int index) {}

extern "C" int vdpGetBorderX(void* vdp) { return 0; }
extern "C" int vdpGetStatus(void* vdp, int reg) { 
    if (reg == 2) return 0; // Command engine not busy
    return 0; 
}
extern "C" void vdpSetScreenMode(void* vdp, int mode) {}
extern "C" void vdpSetTimingMode(void* vdp, int mode) {}
extern "C" UInt8 vdpGetColor(void* vdpCmdState) { 
    return 0; 
}
