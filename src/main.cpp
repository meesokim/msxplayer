#include "msxplay.h"
#include "FrameBuffer.h"
#include "unzip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <signal.h>

extern "C" {
    extern R800* cpu;
    extern UInt32* boardSysTime;
    extern UInt32 currentBoardTime;
    void boardSetInt(UInt32 mask);
    void boardClearInt(UInt32 mask);
    void ioPortRegister(int port, IoPortRead read, IoPortWrite write, void* ref);
    void vdpSetSpritesEnable(int enable);
    void vdpSetNoSpriteLimits(int enable);
    UInt8 readIoPort(void* ref, UInt16 address);
    void  writeIoPort(void* ref, UInt16 address, UInt8 value);
    UInt8* vdpGetVramPtr();
    void ay8910WriteAddress(AY8910*, UInt16, UInt8);
    void ay8910WriteData(AY8910*, UInt16, UInt8);
    UInt8 ay8910ReadData(AY8910*, UInt16);
}

extern void initVideo();
extern void initSound();
extern "C" void RefreshScreen(int);
extern "C" void* ioPortGetRef(int port);
extern "C" void saveScreenshot(const char* filename);
extern "C" void updateSound();

bool debugMode = false;
bool vramViewerMode = false;
static UInt8 bios[0x8000]; 
static UInt8 primarySlot = 0x00; 
static int romActualSize = 0;

struct MSXTimer {
    void (*callback)(void*, UInt32);
    void* ref;
    UInt32 time;
    bool active;
};
static std::vector<MSXTimer*> timers;

extern "C" void* boardTimerCreate(void (*callback)(void*, UInt32), void* ref) {
    MSXTimer* t = new MSXTimer{callback, ref, 0, false};
    timers.push_back(t);
    return t;
}

extern "C" void boardTimerAdd(void* timer, UInt32 time) {
    MSXTimer* t = (MSXTimer*)timer;
    t->time = time;
    t->active = true;
}

extern "C" void boardTimerDestroy(void* timer) {
    for (size_t i = 0; i < timers.size(); ++i) {
        if (timers[i] == (MSXTimer*)timer) {
            delete timers[i];
            timers.erase(timers.begin() + i);
            break;
        }
    }
}

bool updateTimers(UInt32 time) {
    bool executed = false;
    for (size_t i = 0; i < timers.size(); ++i) {
        if (timers[i]->active && timers[i]->time <= time) {
            timers[i]->active = false;
            timers[i]->callback(timers[i]->ref, time);
            executed = true;
        }
    }
    return executed;
}

extern "C" {
    UInt8 ram[0x10000];
    UInt8 rom[0xC000]; 
}

bool loadRom(const char* filename) {
    memset(rom, 0xFF, sizeof(rom));
    if (strstr(filename, ".zip") || strstr(filename, ".ZIP")) {
        unzFile uf = unzOpen(filename);
        if (!uf) return false;
        if (unzGoToFirstFile(uf) != UNZ_OK) { unzClose(uf); return false; }
        do {
            unz_file_info file_info;
            char name_in_zip[256];
            if (unzGetCurrentFileInfo(uf, &file_info, name_in_zip, sizeof(name_in_zip), NULL, 0, NULL, 0) != UNZ_OK) break;
            if (strstr(name_in_zip, ".rom") || strstr(name_in_zip, ".ROM")) {
                if (unzOpenCurrentFile(uf) != UNZ_OK) break;
                romActualSize = file_info.uncompressed_size;
                if (romActualSize > 0xC000) romActualSize = 0xC000;
                unzReadCurrentFile(uf, rom, romActualSize);
                unzCloseCurrentFile(uf);
                unzClose(uf);
                if (debugMode) printf("Loaded %s from zip, size %d\n", name_in_zip, romActualSize);
                return true;
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
        unzClose(uf);
        return false;
    } else {
        FILE* f = fopen(filename, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        romActualSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (romActualSize > 0xC000) romActualSize = 0xC000;
        fread(rom, 1, romActualSize, f);
        fclose(f);
        if (debugMode) printf("Loaded %s, size %d\n", filename, romActualSize);
        return true;
    }
}

UInt8 readMemory(void* ref, UInt16 address) {
    int page = address >> 14;
    int slot = (primarySlot >> (page * 2)) & 0x03;
    
    if (slot == 0) {
        if (address < 0x8000) return bios[address];
        return ram[address];
    } 
    else if (slot == 1 || slot == 2) {
        if (address >= 0x4000 && address < 0xC000) {
            UInt16 offset = address - 0x4000;
            if (romActualSize <= 0x4000) offset &= 0x3FFF;
            
            if (debugMode && (address == 0x4000 || address == 0x8000)) {
                static int lastAddr = -1;
                if (address != lastAddr) {
                    printf("BIOS reading ROM Header at 0x%04X (Slot %d): Found %02X\n", address, slot, rom[offset]);
                    lastAddr = address;
                }
            }
            return rom[offset];
        }
        return 0xFF;
    } 
    else if (slot == 3) {
        return ram[address];
    }
    return 0xFF;
}

void writeMemory(void* ref, UInt16 address, UInt8 value) {
    ram[address] = value;
}

extern "C" UInt8 r800ReadIo(void* ref, UInt16 address) {
    UInt8 port = address & 0xFF;
    if (port == 0xA8) return primarySlot;
    UInt8 val = readIoPort(ref, address);
    if (port == 0x99) boardClearInt(1);
    return val;
}

extern "C" void r800WriteIo(void* ref, UInt16 address, UInt8 value) {
    UInt8 port = address & 0xFF;
    if (port == 0xA8) {
        primarySlot = value;
        return;
    }
    writeIoPort(ref, address, value);
}

void handleHLE(R800* cpu) {
    if (cpu->regs.PC.W == 0x005C) {
        UInt16 count = cpu->regs.BC.W;
        UInt16 dest = cpu->regs.DE.W;
        UInt16 src = cpu->regs.HL.W;
        writeIoPort(NULL, 0x99, dest & 0xFF);
        writeIoPort(NULL, 0x99, (dest >> 8) | 0x40);
        for (UInt16 i = 0; i < count; i++) writeIoPort(NULL, 0x98, readMemory(NULL, src + i));
        cpu->regs.HL.W += count; cpu->regs.DE.W += count; cpu->regs.BC.W = 0;
        UInt16 sp = cpu->regs.SP.W;
        cpu->regs.PC.W = readMemory(NULL, sp) | (readMemory(NULL, sp + 1) << 8);
        cpu->regs.SP.W = sp + 2;
    }
}

static UInt8 selectedRow = 0;
static UInt8 keyMatrix[16];

static UInt8 keyboardRead(void* ref, UInt16 port) {
    if (selectedRow < 16) return keyMatrix[selectedRow];
    return 0xFF;
}
static void keyboardWrite(void* ref, UInt16 port, UInt8 value) {
    selectedRow = value & 0x0F;
}

static UInt8 psgSelectedReg = 0;
static UInt8 psgPortB = 0;
static UInt8 psgRead(void* arg, UInt16 port) {
    if (port == 0) { // Port A
        UInt8 val = 0x3F;
        const Uint8* state = SDL_GetKeyboardState(NULL);
        if (!(psgPortB & 0x40)) {
            if (state[SDL_SCANCODE_UP])    val &= ~0x01;
            if (state[SDL_SCANCODE_DOWN])  val &= ~0x02;
            if (state[SDL_SCANCODE_LEFT])  val &= ~0x04;
            if (state[SDL_SCANCODE_RIGHT]) val &= ~0x08;
            if (state[SDL_SCANCODE_SPACE] || state[SDL_SCANCODE_Z]) val &= ~0x10;
            if (state[SDL_SCANCODE_X] || state[SDL_SCANCODE_LSHIFT]) val &= ~0x20;
        }
        return val;
    }
    return 0xFF;
}

static void psgWrite(void* arg, UInt16 port, UInt8 val) {
    if (port == 1) psgPortB = val; 
}

static void myPsgWriteAddr(void* ref, UInt16 port, UInt8 val) {
    psgSelectedReg = val & 0x0F;
    ay8910WriteAddress((AY8910*)ref, port, val);
}

void updateKeyboard() {
    const Uint8* state = SDL_GetKeyboardState(NULL);
    memset(keyMatrix, 0xFF, sizeof(keyMatrix));
    if (state[SDL_SCANCODE_0]) keyMatrix[0] &= ~0x01;
    if (state[SDL_SCANCODE_1]) keyMatrix[0] &= ~0x02;
    if (state[SDL_SCANCODE_2]) keyMatrix[0] &= ~0x04;
    if (state[SDL_SCANCODE_3]) keyMatrix[0] &= ~0x08;
    if (state[SDL_SCANCODE_4]) keyMatrix[0] &= ~0x10;
    if (state[SDL_SCANCODE_5]) keyMatrix[0] &= ~0x20;
    if (state[SDL_SCANCODE_6]) keyMatrix[0] &= ~0x40;
    if (state[SDL_SCANCODE_7]) keyMatrix[0] &= ~0x80;
    if (state[SDL_SCANCODE_8]) keyMatrix[1] &= ~0x01;
    if (state[SDL_SCANCODE_9]) keyMatrix[1] &= ~0x02;
    if (state[SDL_SCANCODE_MINUS]) keyMatrix[1] &= ~0x04;
    if (state[SDL_SCANCODE_EQUALS]) keyMatrix[1] &= ~0x08;
    if (state[SDL_SCANCODE_BACKSLASH]) keyMatrix[1] &= ~0x10;
    if (state[SDL_SCANCODE_LEFTBRACKET]) keyMatrix[1] &= ~0x20;
    if (state[SDL_SCANCODE_RIGHTBRACKET]) keyMatrix[1] &= ~0x40;
    if (state[SDL_SCANCODE_SEMICOLON]) keyMatrix[1] &= ~0x80;
    if (state[SDL_SCANCODE_APOSTROPHE]) keyMatrix[2] &= ~0x01;
    if (state[SDL_SCANCODE_COMMA])  keyMatrix[2] &= ~0x04;
    if (state[SDL_SCANCODE_PERIOD]) keyMatrix[2] &= ~0x08;
    if (state[SDL_SCANCODE_SLASH])  keyMatrix[2] &= ~0x10;
    if (state[SDL_SCANCODE_A])      keyMatrix[2] &= ~0x40;
    if (state[SDL_SCANCODE_B])      keyMatrix[2] &= ~0x80;
    if (state[SDL_SCANCODE_C]) keyMatrix[3] &= ~0x01;
    if (state[SDL_SCANCODE_D]) keyMatrix[3] &= ~0x02;
    if (state[SDL_SCANCODE_E]) keyMatrix[3] &= ~0x04;
    if (state[SDL_SCANCODE_F]) keyMatrix[3] &= ~0x08;
    if (state[SDL_SCANCODE_G]) keyMatrix[3] &= ~0x10;
    if (state[SDL_SCANCODE_H]) keyMatrix[3] &= ~0x20;
    if (state[SDL_SCANCODE_I]) keyMatrix[3] &= ~0x40;
    if (state[SDL_SCANCODE_J]) keyMatrix[3] &= ~0x80;
    if (state[SDL_SCANCODE_K]) keyMatrix[4] &= ~0x01;
    if (state[SDL_SCANCODE_L]) keyMatrix[4] &= ~0x02;
    if (state[SDL_SCANCODE_M]) keyMatrix[4] &= ~0x04;
    if (state[SDL_SCANCODE_N]) keyMatrix[4] &= ~0x08;
    if (state[SDL_SCANCODE_O]) keyMatrix[4] &= ~0x10;
    if (state[SDL_SCANCODE_P]) keyMatrix[4] &= ~0x20;
    if (state[SDL_SCANCODE_Q]) keyMatrix[4] &= ~0x40;
    if (state[SDL_SCANCODE_R]) keyMatrix[4] &= ~0x80;
    if (state[SDL_SCANCODE_S]) keyMatrix[5] &= ~0x01;
    if (state[SDL_SCANCODE_T]) keyMatrix[5] &= ~0x02;
    if (state[SDL_SCANCODE_U]) keyMatrix[5] &= ~0x04;
    if (state[SDL_SCANCODE_V]) keyMatrix[5] &= ~0x08;
    if (state[SDL_SCANCODE_W]) keyMatrix[5] &= ~0x10;
    if (state[SDL_SCANCODE_X]) keyMatrix[5] &= ~0x20;
    if (state[SDL_SCANCODE_Y]) keyMatrix[5] &= ~0x40;
    if (state[SDL_SCANCODE_Z]) keyMatrix[5] &= ~0x80;
    if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) keyMatrix[6] &= ~0x01;
    if (state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL])   keyMatrix[6] &= ~0x02;
    if (state[SDL_SCANCODE_LALT]) keyMatrix[6] &= ~0x04;
    if (state[SDL_SCANCODE_F1]) keyMatrix[6] &= ~0x20;
    if (state[SDL_SCANCODE_F2]) keyMatrix[6] &= ~0x40;
    if (state[SDL_SCANCODE_F3]) keyMatrix[6] &= ~0x80;
    if (state[SDL_SCANCODE_F4]) keyMatrix[7] &= ~0x01;
    if (state[SDL_SCANCODE_F5]) keyMatrix[7] &= ~0x02;
    if (state[SDL_SCANCODE_ESCAPE]) keyMatrix[7] &= ~0x04;
    if (state[SDL_SCANCODE_TAB])    keyMatrix[7] &= ~0x08;
    if (state[SDL_SCANCODE_BACKSPACE]) keyMatrix[7] &= ~0x20;
    if (state[SDL_SCANCODE_RETURN]) keyMatrix[7] &= ~0x80;
    if (state[SDL_SCANCODE_SPACE]) keyMatrix[8] &= ~0x01;
    if (state[SDL_SCANCODE_HOME])  keyMatrix[8] &= ~0x02;
    if (state[SDL_SCANCODE_INSERT]) keyMatrix[8] &= ~0x04;
    if (state[SDL_SCANCODE_DELETE]) keyMatrix[8] &= ~0x08;
    if (state[SDL_SCANCODE_LEFT])  keyMatrix[8] &= ~0x10;
    if (state[SDL_SCANCODE_UP])    keyMatrix[8] &= ~0x20;
    if (state[SDL_SCANCODE_DOWN])  keyMatrix[8] &= ~0x40;
    if (state[SDL_SCANCODE_RIGHT]) keyMatrix[8] &= ~0x80;
}

extern "C" void saveVramSc2(const char* filename) {
    UInt8* vram = vdpGetVramPtr();
    if (!vram) return;
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    UInt8 header[7] = { 0xFE, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00 };
    fwrite(header, 1, 7, f);
    fwrite(vram, 1, 0x4000, f);
    fclose(f);
    if (debugMode) printf("\nVRAM saved to %s\n", filename);
}

void runVdpDiagnostics();

int main(int argc, char* argv[]) {
    const char* romFile = "Bank Panic (1985) (Pony Cannon) (J).rom";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) debugMode = true;
        else if (strcmp(argv[i], "-v") == 0) vramViewerMode = true;
        else romFile = argv[i];
    }

    if (debugMode) printf("msxplay starting in debug mode...\n");

    if (!loadRom(romFile)) return 1;

    FILE* bf = fopen("cbios_main_msx1.rom", "rb");
    if (!bf) return 1;
    fread(bios, 1, 0x8000, bf); fclose(bf);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0) return 1;
    initVideo(); initSound();
    vdpCreate(VDP_MSX, VDP_TMS99x8A, VDP_SYNC_60HZ, 1);
    vdpSetSpritesEnable(1);
    vdpSetNoSpriteLimits(1);

    if (debugMode) runVdpDiagnostics();

    AY8910* psg = ay8910Create(NULL, AY8910_MSX, PSGTYPE_AY8910, 0, NULL);
    ay8910SetIoPort(psg, psgRead, psgRead, psgWrite, NULL);
    ioPortRegister(0xA0, NULL, (IoPortWrite)myPsgWriteAddr, psg);
    ioPortRegister(0xA1, NULL, (IoPortWrite)ay8910WriteData, psg);
    ioPortRegister(0xA2, (IoPortRead)ay8910ReadData, NULL, psg);

    cpu = r800Create(0, (R800ReadCb)readMemory, (R800WriteCb)writeMemory, (R800ReadCb)r800ReadIo, (R800WriteCb)r800WriteIo, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    r800Reset(cpu, 0);
    primarySlot = 0x00; 
    ioPortRegister(0xA9, (IoPortRead)keyboardRead, NULL, NULL);
    ioPortRegister(0xAA, NULL, (IoPortWrite)keyboardWrite, NULL);

    bool quit = false; SDL_Event e; Uint32 lastTime = SDL_GetTicks();
    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.key.keysym.sym == SDLK_PRINTSCREEN) {
                    saveVramSc2("capture.sc2");
                    saveScreenshot("capture.bmp");
                }
            }
        }
        updateKeyboard();
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - lastTime >= 16) {
            for (int i = 0; i < 262; i++) {
                currentBoardTime += 1368;
                r800ExecuteUntil(cpu, currentBoardTime);
                handleHLE(cpu);
                while (updateTimers(currentBoardTime));
            }
            RefreshScreen(0);
            updateSound();
            lastTime = currentTime;
        } else { SDL_Delay(1); }
    }
    SDL_Quit(); return 0;
}
