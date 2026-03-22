
#ifndef MSXPLAY_H
#define MSXPLAY_H

#include "MsxTypes.h"
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "R800.h"
#include "VDP.h"
#include "AY8910.h"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern R800* cpu;
extern UInt8 ram[0x10000];
extern UInt8 rom[0xC000];
extern bool debugMode;
extern bool vramViewerMode;
extern bool scanlinesEnabled;

#ifdef __cplusplus
}
#endif

typedef UInt8 (*IoPortRead)(void*, UInt16);
typedef void  (*IoPortWrite)(void*, UInt16, UInt8);

// Memory access
UInt8 readMemory(void* ref, UInt16 address);
void writeMemory(void* ref, UInt16 address, UInt8 value);

// I/O access
extern "C" UInt8 readIoPort(void* ref, UInt16 address);
extern "C" void  writeIoPort(void* ref, UInt16 address, UInt8 value);
extern "C" void* ioPortGetRef(int port);

#endif
