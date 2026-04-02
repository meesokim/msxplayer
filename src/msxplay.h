
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
extern UInt8* romData;
extern int romSize;
extern bool debugMode;
extern bool vramViewerMode;
extern bool scanlinesEnabled;
extern bool g_isErrorGame;
extern UInt8 primarySlot;
extern UInt8 bios[0x8000];

void* boardTimerCreate(void (*callback)(void*, UInt32), void* ref);
void boardTimerAdd(void* timer, UInt32 time);
void boardTimerDestroy(void* timer);

extern "C" UInt8* vdpGetVramPtr();
void vdpTraceOpen();
void vdpTraceClose();

#ifdef __cplusplus
}
#endif

enum MapperType {
    MAPPER_NONE,
    MAPPER_KONAMI,
    MAPPER_KONAMI_SCC,
    MAPPER_ASCII8,
    MAPPER_ASCII8_SRAM2,
    MAPPER_ASCII16,
    /** openMSX RomMSXWrite: ASCII16 + bank writes at 6FFFh and 7FFFh. */
    MAPPER_MSXWRITE,
    /** openMSX RomAscii16_2 (ASCII16SRAM2): 2 KiB SRAM, select with bank value 0x10. */
    MAPPER_ASCII16_SRAM2,
    MAPPER_MIRRORED,
    /** openMSX RomType PAGE2: ROM only at 8000h–BFFFh (16 KiB window). */
    MAPPER_PAGE2,
    /** openMSX RomRType: Irem R-Type; 4000h–7FFFh fixed bank 0x17; 8000h–BFFFh via writes 4000h–7FFFh. */
    MAPPER_RTYPE
};
extern MapperType romMapper;
extern int romBanks[4];

typedef UInt8 (*IoPortRead)(void*, UInt16);
typedef void  (*IoPortWrite)(void*, UInt16, UInt8);

// Memory access
UInt8 readMemory(void* ref, UInt16 address);
void writeMemory(void* ref, UInt16 address, UInt8 value);

// I/O access
extern "C" UInt8 readIoPort(void* ref, UInt16 address);
extern "C" void  writeIoPort(void* ref, UInt16 address, UInt8 value);
extern "C" void* ioPortGetRef(int port);

/** ROM list menu: lines per page (keep in sync with DrawMenu in video.cpp). */
#define MSXPLAY_MENU_PAGE_SIZE 20

#endif
