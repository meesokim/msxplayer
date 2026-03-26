
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef unsigned char UInt8;
typedef unsigned short UInt16;
typedef unsigned int UInt32;

enum MapperType { MAPPER_NONE, MAPPER_KONAMI, MAPPER_KONAMI_SCC, MAPPER_ASCII8, MAPPER_ASCII8_SRAM2, MAPPER_ASCII16, MAPPER_MIRRORED, MAPPER_PAGE2 };

// Mocking required state
UInt8 ram[0x10000];
UInt8* romData = NULL;
int romSize = 0;
MapperType romMapper = MAPPER_NONE;
int romBanks[4] = {0, 1, 2, 3};
UInt8 primarySlot = 0;
UInt8 bios[0x8000];

static UInt8 readMirroredRomAt4000(UInt16 address) {
    if (!romData || address < 0x4000 || address >= 0xC000) return 0xFF;
    const unsigned region = ((unsigned)address >> 13);
    const unsigned firstPage = 2;
    unsigned num8k = (unsigned)(romSize / 0x2000);
    if (num8k < 1) num8k = 1;
    unsigned romPage = region - firstPage;
    unsigned block = (romPage < num8k) ? romPage : (romPage % num8k);
    unsigned off = block * 0x2000u + (address & 0x1FFFu);
    if (off < (unsigned)romSize) return romData[off];
    return 0xFF;
}

// Logic under test (copied from main.cpp to be standalone)
UInt8 readMemory(void* ref, UInt16 address) {
    int page = address >> 14;
    int slot = (primarySlot >> (page * 2)) & 0x03;
    if (slot == 0) return (address < 0x8000) ? bios[address] : ram[address];
    if (slot == 1 || slot == 2) {
        if (!romData) return 0xFF;
        if (romMapper == MAPPER_NONE) {
            if (address >= 0x4000) {
                UInt16 off = address - 0x4000;
                if (romSize <= 0x4000) off &= 0x3FFF;
                if (off < romSize) return romData[off];
            }
        } else if (romMapper == MAPPER_MIRRORED) {
            if (address >= 0x4000 && address < 0xC000) return readMirroredRomAt4000(address);
        } else {
            if (address >= 0x4000 && address < 0xC000) {
                int bankIdx = (address - 0x4000) / 0x2000;
                int bank = romBanks[bankIdx];
                int offset = (bank * 0x2000) + (address % 0x2000);
                if (offset < (int)romSize) return romData[offset];
            }
        }
        return 0xFF;
    }
    return (slot == 3) ? ram[address] : 0xFF;
}

void writeMemory(void* ref, UInt16 address, UInt8 value) {
    int page = address >> 14;
    int slot = (primarySlot >> (page * 2)) & 0x03;
    if ((slot == 1 || slot == 2) && address >= 0x4000 && address < 0xC000) {
        if (romMapper == MAPPER_KONAMI) {
            if (address == 0x6000) romBanks[1] = value % (romSize / 0x2000);
            else if (address == 0x8000) romBanks[2] = value % (romSize / 0x2000);
            else if (address == 0xA000) romBanks[3] = value % (romSize / 0x2000);
        } else if (romMapper == MAPPER_ASCII8) {
            if (address >= 0x6000 && address < 0x6800) romBanks[0] = value % (romSize / 0x2000);
            else if (address >= 0x6800 && address < 0x7000) romBanks[1] = value % (romSize / 0x2000);
            else if (address >= 0x7000 && address < 0x7800) romBanks[2] = value % (romSize / 0x2000);
            else if (address >= 0x7800 && address < 0x8000) romBanks[3] = value % (romSize / 0x2000);
        }
    }
    ram[address] = value; 
}

void test_mapper_konami() {
    printf("Testing Konami Mapper... ");
    romSize = 128 * 1024;
    romData = (UInt8*)malloc(romSize);
    for(int i=0; i<romSize; i++) romData[i] = i / 0x2000;
    
    romMapper = MAPPER_KONAMI;
    primarySlot = 0x14; // Page 1,2 in Slot 1. Page 2 is 0x8000-0xBFFF. 
    // Bits for primarySlot: P3 P2 P1 P0 (2 bits each)
    // Slot 1 for Page 1: 0x04 (00 00 01 00)
    // Slot 1 for Page 2: 0x10 (00 01 00 00)
    // So 0x14 maps Page 1 and Page 2 to Slot 1.
    romBanks[0] = 0; romBanks[1] = 1; romBanks[2] = 2; romBanks[3] = 3;

    assert(readMemory(NULL, 0x4000) == 0);
    assert(readMemory(NULL, 0x6000) == 1);
    assert(readMemory(NULL, 0x8000) == 2);
    assert(readMemory(NULL, 0xA000) == 3);

    writeMemory(NULL, 0x6000, 10);
    assert(romBanks[1] == 10);
    assert(readMemory(NULL, 0x6000) == 10);

    writeMemory(NULL, 0x8000, 5);
    assert(romBanks[2] == 5);
    assert(readMemory(NULL, 0x8000) == 5);

    free(romData); romData = NULL;
    printf("OK\n");
}

void test_mapper_ascii8() {
    printf("Testing ASCII8 Mapper... ");
    romSize = 128 * 1024;
    romData = (UInt8*)malloc(romSize);
    for(int i=0; i<romSize; i++) romData[i] = i / 0x2000;

    romMapper = MAPPER_ASCII8;
    primarySlot = 0x14; // Slot 1 for Page 1,2
    romBanks[0] = 0; romBanks[1] = 1; romBanks[2] = 2; romBanks[3] = 3;

    writeMemory(NULL, 0x6000, 15);
    assert(romBanks[0] == 15);
    assert(readMemory(NULL, 0x4000) == 15);

    writeMemory(NULL, 0x6800, 7);
    assert(romBanks[1] == 7);
    assert(readMemory(NULL, 0x6000) == 7);

    free(romData); romData = NULL;
    printf("OK\n");
}

void test_mapper_mirrored() {
    printf("Testing Mirrored (plain) ROM... ");
    romSize = 0x4000; /* 16 KiB -> 2x8K, tiles over 0x4000-0xBFFF */
    romData = (UInt8*)malloc(romSize);
    memset(romData, 0, romSize);
    romData[0] = 0xAA;           /* first byte of 8K block 0 */
    romData[0x2000] = 0xBB;      /* first byte of 8K block 1 */
    romMapper = MAPPER_MIRRORED;
    primarySlot = 0x14;

    assert(readMemory(NULL, 0x4000) == 0xAA);
    assert(readMemory(NULL, 0x6000) == 0xBB);
    /* 0x8000 is third 8K window -> mirror block 0 */
    assert(readMemory(NULL, 0x8000) == 0xAA);
    assert(readMemory(NULL, 0xA000) == 0xBB);

    free(romData); romData = NULL;
    printf("OK\n");
}

void test_vram_to_rgb_logic() {
    printf("Testing VRAM to RGB Logic (Screen 2)... ");
    UInt8 vram[0x4000];
    UInt16 palette[16];
    UInt8 regs[16];
    memset(vram, 0, 0x4000);
    for(int i=0; i<16; i++) palette[i] = i;

    regs[2] = 0x06; // NT at 0x1800
    regs[4] = 0x03; // PG at 0x0000
    regs[3] = 0xFF; // CT at 0x2000
    
    vram[0x0000] = 0x80; // Pattern 10000000
    vram[0x2000] = 0xF1; // FG=15, BG=1
    vram[0x1800] = 0x00; // Char 0

    // Manual extraction based on Screen 2 logic in video.cpp
    int nt = (regs[2] << 10) & 0x3FFF;
    int pb = (regs[4] & 0x3C) << 11;
    int pm = ((regs[4] & 0x03) << 11) | 0x7FF;
    int cb = (regs[3] & 0x80) << 6;
    int cm = ((regs[3] & 0x7F) << 6) | 0x3F;

    int y = 0;
    int zone = (y / 64) << 11;
    int py = y % 8;
    int row = (y / 8) * 32;
    int tx = 0;
    
    int idx = zone | (vram[(nt + row + tx) & 0x3FFF] << 3) | py;
    UInt8 pat = vram[(pb | (idx & pm)) & 0x3FFF];
    UInt8 col = vram[(cb | (idx & cm)) & 0x3FFF];
    
    assert(pat == 0x80);
    assert(col == 0xF1);
    
    UInt16 pfg = palette[col >> 4];
    UInt16 pbg = palette[col & 0x0F];
    
    assert(pfg == 15);
    assert(pbg == 1);
    
    UInt16 pixel0 = (pat & 0x80) ? pfg : pbg;
    UInt16 pixel1 = (pat & 0x40) ? pfg : pbg;
    
    assert(pixel0 == 15);
    assert(pixel1 == 1);

    printf("OK\n");
}

int main() {
    test_mapper_konami();
    test_mapper_ascii8();
    test_mapper_mirrored();
    test_vram_to_rgb_logic();
    printf("Verification logic SUCCESS\n");
    return 0;
}
