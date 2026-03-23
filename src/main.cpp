#include "msxplay.h"
#include "FrameBuffer.h"
#include "unzip.h"
#include "bios_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#endif

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
    void ay8910SetIoPort(AY8910* ay8910, AY8910ReadCb readCb, AY8910ReadCb pollCb, AY8910WriteCb writeCb, void* arg);
}

extern void initVideo();
extern void initSound();
extern "C" void RefreshScreen(int);
extern "C" void DrawMenu(const std::vector<std::string>& files, int selected, int offset);
extern "C" void* ioPortGetRef(int port);
extern "C" void saveScreenshot(const char* filename);
extern "C" void updateSound();
extern "C" SDL_Window* getMainWindow();

bool debugMode = false;
bool vramViewerMode = false;
bool scanlinesEnabled = true;
UInt8 primarySlot = 0x00; 
static int romSizeTotal = 0; // Renamed from romActualSize
static AY8910* psg = NULL;
static bool vdpCreated = false;

enum AppState { STATE_MENU, STATE_EMU };
static AppState appState = STATE_MENU;

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
        if (timers[i]->active && (Int32)(time - timers[i]->time) >= 0) {
            timers[i]->active = false;
            timers[i]->callback(timers[i]->ref, time);
            executed = true;
        }
    }
    return executed;
}

extern "C" {
    UInt8 ram[0x10000];
    UInt8* romData = NULL;
    int romSize = 0;
}

MapperType romMapper = MAPPER_NONE;
int romBanks[4] = {0, 1, 2, 3};

static void detectMapper() {
    romMapper = MAPPER_NONE;
    if (romSize <= 0x8000) return; // 32KB or less is not a MegaROM
    
    // Default to Konami
    romMapper = MAPPER_KONAMI;
    
    // Heuristic: Check for 'AB' signature at 0x4000
    if (romSize > 0x4000 && romData[0] == 'A' && romData[1] == 'B') {
        // Many ASCII games also have 'AB' signature.
        // Konami games often have specific strings.
        // For now, Konami is a safe default for most MegaROMs.
    }
    
    // If ROM size is exactly 128KB or 256KB and not from Konami, 
    // it *might* be ASCII8. This is a very rough heuristic.
    // In a real emulator, we'd use a checksum database.
    
    romBanks[0] = 0; romBanks[1] = 1; romBanks[2] = 2; romBanks[3] = 3;
}

bool loadRom(const char* filename) {
    if (romData) { free(romData); romData = NULL; }
    if (strstr(filename, ".zip") || strstr(filename, ".ZIP")) {
        unzFile uf = unzOpen(filename);
        if (!uf) return false;
        if (unzGoToFirstFile(uf) != UNZ_OK) { unzClose(uf); return false; }
        do {
            unz_file_info info; char name[256];
            if (unzGetCurrentFileInfo(uf, &info, name, 256, NULL, 0, NULL, 0) != UNZ_OK) break;
            std::string low = name; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low.find(".rom") != std::string::npos || low.find(".mx1") != std::string::npos || low.find(".bin") != std::string::npos) {
                if (unzOpenCurrentFile(uf) != UNZ_OK) break;
                romSize = info.uncompressed_size;
                romData = (UInt8*)malloc(romSize);
                unzReadCurrentFile(uf, romData, romSize);
                unzCloseCurrentFile(uf); unzClose(uf);
                detectMapper();
                return true;
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
        unzClose(uf); return false;
    } else {
        FILE* f = fopen(filename, "rb"); if (!f) return false;
        fseek(f, 0, SEEK_END); romSize = ftell(f); fseek(f, 0, SEEK_SET);
        romData = (UInt8*)malloc(romSize);
        fread(romData, 1, romSize, f); fclose(f);
        detectMapper();
        printf("loadRom: Loaded '%s', size=%d, mapper=%d\n", filename, romSize, romMapper); fflush(stdout);
        return true;
    }
}

UInt8 readMemory(void* ref, UInt16 address) {
    int page = address >> 14;
    int slot = (primarySlot >> (page * 2)) & 0x03;
    if (slot == 0) {
        if (address < 0x8000) return bios[address];
        if (address < 0xC000) return bios_logo[address - 0x8000];
        return ram[address];
    }
    if (slot == 1 || slot == 2) {
        if (!romData) return 0xFF;
        if (romMapper == MAPPER_NONE) {
            bool startsAtZero = ((primarySlot & 0x03) == 0x01);
            if (startsAtZero) {
                if (address < romSize) return romData[address];
            } else { // Starts at 0x4000
                if (address >= 0x4000) {
                    UInt16 off = address - 0x4000;
                    if (romSize <= 0x4000) off &= 0x3FFF;
                    if (off < romSize) return romData[off];
                }
            }
        } else {
            // Konami, Konami SCC, ASCII8 (8KB banks)
            if (address >= 0x4000 && address < 0xC000) {
                int bankIdx = (address - 0x4000) / 0x2000;
                int bank = romBanks[bankIdx];
                int offset = (bank * 0x2000) + (address % 0x2000);
                if (offset < romSize) return romData[offset];
            }
        }
        return 0xFF;
    }
    return (slot == 3) ? ram[address] : 0xFF;
}

void writeMemory(void* ref, UInt16 address, UInt8 value) {
    int page = address >> 14;
    int slot = (primarySlot >> (page * 2)) & 0x03;

    // First, handle mapper register writes, which are exceptions
    if ((slot == 1 || slot == 2) && romMapper != MAPPER_NONE) {
        if (romMapper == MAPPER_KONAMI) {
            if (address == 0x6000 || address == 0x8000 || address == 0xA000) {
                 if (address == 0x6000) romBanks[1] = value % (romSize / 0x2000);
                 else if (address == 0x8000) romBanks[2] = value % (romSize / 0x2000);
                 else if (address == 0xA000) romBanks[3] = value % (romSize / 0x2000);
                 return;
            }
        } else if (romMapper == MAPPER_ASCII8) {
            if (address >= 0x6000 && address < 0x8000) {
                if (address >= 0x6000 && address < 0x6800) romBanks[0] = value;
                else if (address >= 0x6800 && address < 0x7000) romBanks[1] = value;
                else if (address >= 0x7000 && address < 0x7800) romBanks[2] = value;
                else if (address >= 0x7800 && address < 0x8000) romBanks[3] = value;
                return;
            }
        }
    }

    // If not a mapper write, check if the area is RAM
    if (slot == 3) {
        ram[address] = value;
        return;
    }
    if (slot == 0 && address >= 0xc000) {
        ram[address] = value;
        return;
    }

    // Otherwise, it's a write to a ROM area, so ignore it.
}

extern "C" UInt8 r800ReadIo(void* ref, UInt16 address) {
    UInt8 val = readIoPort(ref, address);
    if ((address & 0xFF) == 0x99) boardClearInt(1);
    return val;
}

extern "C" void r800WriteIo(void* ref, UInt16 address, UInt8 value) {
    writeIoPort(ref, address, value);
}

static UInt8 selectedRow = 0;
static UInt8 keyMatrix[16];
static UInt8 keyboardRead(void* ref, UInt16 port) { return (selectedRow < 16) ? keyMatrix[selectedRow] : 0xFF; }
static void keyboardWrite(void* ref, UInt16 port, UInt8 value) { selectedRow = value & 0x0F; }

static UInt8 psgPortB = 0;
static UInt8 psgRead(void* arg, UInt16 port) {
    if (port == 0) { // Port A (Joystick)
        UInt8 val = 0x3F; const Uint8* s = SDL_GetKeyboardState(NULL);
        if (!(psgPortB & 0x40)) {
            if (s[SDL_SCANCODE_UP]) val &= ~0x01; if (s[SDL_SCANCODE_DOWN]) val &= ~0x02;
            if (s[SDL_SCANCODE_LEFT]) val &= ~0x04; if (s[SDL_SCANCODE_RIGHT]) val &= ~0x08;
            if (s[SDL_SCANCODE_SPACE] || s[SDL_SCANCODE_Z]) val &= ~0x10;
            if (s[SDL_SCANCODE_X] || s[SDL_SCANCODE_LSHIFT]) val &= ~0x20;
        }
        return val;
    }
    return 0xFF;
}
static void psgWrite(void* arg, UInt16 port, UInt8 val) { if (port == 1) psgPortB = val; }
static void myPsgWriteAddr(void* ref, UInt16 port, UInt8 val) { ay8910WriteAddress((AY8910*)ref, port, val); }

void updateKeyboard() {
    const Uint8* s = SDL_GetKeyboardState(NULL);
    memset(keyMatrix, 0xFF, sizeof(keyMatrix));
    if (s[SDL_SCANCODE_0]) keyMatrix[0] &= ~0x01; if (s[SDL_SCANCODE_1]) keyMatrix[0] &= ~0x02;
    if (s[SDL_SCANCODE_2]) keyMatrix[0] &= ~0x04; if (s[SDL_SCANCODE_3]) keyMatrix[0] &= ~0x08;
    if (s[SDL_SCANCODE_4]) keyMatrix[0] &= ~0x10; if (s[SDL_SCANCODE_5]) keyMatrix[0] &= ~0x20;
    if (s[SDL_SCANCODE_6]) keyMatrix[0] &= ~0x40; if (s[SDL_SCANCODE_7]) keyMatrix[0] &= ~0x80;
    if (s[SDL_SCANCODE_8]) keyMatrix[1] &= ~0x01; if (s[SDL_SCANCODE_9]) keyMatrix[1] &= ~0x02;
    if (s[SDL_SCANCODE_MINUS]) keyMatrix[1] &= ~0x04; if (s[SDL_SCANCODE_EQUALS]) keyMatrix[1] &= ~0x08;
    if (s[SDL_SCANCODE_BACKSLASH]) keyMatrix[1] &= ~0x10; if (s[SDL_SCANCODE_LEFTBRACKET]) keyMatrix[1] &= ~0x20;
    if (s[SDL_SCANCODE_RIGHTBRACKET]) keyMatrix[1] &= ~0x40; if (s[SDL_SCANCODE_SEMICOLON]) keyMatrix[1] &= ~0x80;
    if (s[SDL_SCANCODE_APOSTROPHE]) keyMatrix[2] &= ~0x01; if (s[SDL_SCANCODE_COMMA]) keyMatrix[2] &= ~0x04;
    if (s[SDL_SCANCODE_PERIOD]) keyMatrix[2] &= ~0x08; if (s[SDL_SCANCODE_SLASH]) keyMatrix[2] &= ~0x10;
    if (s[SDL_SCANCODE_A]) keyMatrix[2] &= ~0x40; if (s[SDL_SCANCODE_B]) keyMatrix[2] &= ~0x80;
    if (s[SDL_SCANCODE_C]) keyMatrix[3] &= ~0x01; if (s[SDL_SCANCODE_D]) keyMatrix[3] &= ~0x02;
    if (s[SDL_SCANCODE_E]) keyMatrix[3] &= ~0x04; if (s[SDL_SCANCODE_F]) keyMatrix[3] &= ~0x08;
    if (s[SDL_SCANCODE_G]) keyMatrix[3] &= ~0x10; if (s[SDL_SCANCODE_H]) keyMatrix[3] &= ~0x20;
    if (s[SDL_SCANCODE_I]) keyMatrix[3] &= ~0x40; if (s[SDL_SCANCODE_J]) keyMatrix[3] &= ~0x80;
    if (s[SDL_SCANCODE_K]) keyMatrix[4] &= ~0x01; if (s[SDL_SCANCODE_L]) keyMatrix[4] &= ~0x02;
    if (s[SDL_SCANCODE_M]) keyMatrix[4] &= ~0x04; if (s[SDL_SCANCODE_N]) keyMatrix[4] &= ~0x08;
    if (s[SDL_SCANCODE_O]) keyMatrix[4] &= ~0x10; if (s[SDL_SCANCODE_P]) keyMatrix[4] &= ~0x20;
    if (s[SDL_SCANCODE_Q]) keyMatrix[4] &= ~0x40; if (s[SDL_SCANCODE_R]) keyMatrix[4] &= ~0x80;
    if (s[SDL_SCANCODE_S]) keyMatrix[5] &= ~0x01; if (s[SDL_SCANCODE_T]) keyMatrix[5] &= ~0x02;
    if (s[SDL_SCANCODE_U]) keyMatrix[5] &= ~0x04; if (s[SDL_SCANCODE_V]) keyMatrix[5] &= ~0x08;
    if (s[SDL_SCANCODE_W]) keyMatrix[5] &= ~0x10; if (s[SDL_SCANCODE_X]) keyMatrix[5] &= ~0x20;
    if (s[SDL_SCANCODE_Y]) keyMatrix[5] &= ~0x40; if (s[SDL_SCANCODE_Z]) keyMatrix[5] &= ~0x80;
    if (s[SDL_SCANCODE_LSHIFT] || s[SDL_SCANCODE_RSHIFT]) keyMatrix[6] &= ~0x01;
    if (s[SDL_SCANCODE_LCTRL] || s[SDL_SCANCODE_RCTRL]) keyMatrix[6] &= ~0x02;
    if (s[SDL_SCANCODE_LALT]) keyMatrix[6] &= ~0x04; if (s[SDL_SCANCODE_F1]) keyMatrix[6] &= ~0x20;
    if (s[SDL_SCANCODE_F2]) keyMatrix[6] &= ~0x40; if (s[SDL_SCANCODE_F3]) keyMatrix[6] &= ~0x80;
    if (s[SDL_SCANCODE_F4]) keyMatrix[7] &= ~0x01; if (s[SDL_SCANCODE_F5]) keyMatrix[7] &= ~0x02;
    if (s[SDL_SCANCODE_ESCAPE]) keyMatrix[7] &= ~0x04; if (s[SDL_SCANCODE_TAB]) keyMatrix[7] &= ~0x08;
    if (s[SDL_SCANCODE_BACKSPACE]) keyMatrix[7] &= ~0x20; if (s[SDL_SCANCODE_RETURN]) keyMatrix[7] &= ~0x80;
    if (s[SDL_SCANCODE_SPACE]) keyMatrix[8] &= ~0x01; if (s[SDL_SCANCODE_HOME]) keyMatrix[8] &= ~0x02;
    if (s[SDL_SCANCODE_INSERT]) keyMatrix[8] &= ~0x04; if (s[SDL_SCANCODE_DELETE]) keyMatrix[8] &= ~0x08;
    if (s[SDL_SCANCODE_LEFT]) keyMatrix[8] &= ~0x10; if (s[SDL_SCANCODE_UP]) keyMatrix[8] &= ~0x20;
    if (s[SDL_SCANCODE_DOWN]) keyMatrix[8] &= ~0x40; if (s[SDL_SCANCODE_RIGHT]) keyMatrix[8] &= ~0x80;
}

void startEmulator() {
    if (!vdpCreated) { vdpCreate(VDP_MSX, VDP_TMS99x8A, VDP_SYNC_60HZ, 1); vdpCreated = true; }
    if (!psg) { psg = ay8910Create(NULL, AY8910_MSX, PSGTYPE_AY8910, 0, NULL); ay8910SetIoPort(psg, (AY8910ReadCb)psgRead, (AY8910ReadCb)psgRead, (AY8910WriteCb)psgWrite, NULL); }
    memset(ram, 0, sizeof(ram));
    ioPortRegister(0xA0, NULL, (IoPortWrite)myPsgWriteAddr, psg); ioPortRegister(0xA1, NULL, (IoPortWrite)ay8910WriteData, psg); ioPortRegister(0xA2, (IoPortRead)ay8910ReadData, NULL, psg);
    ioPortRegister(0xA9, (IoPortRead)keyboardRead, NULL, NULL); ioPortRegister(0xAA, NULL, (IoPortWrite)keyboardWrite, NULL);
    if (!cpu) cpu = r800Create(0, (R800ReadCb)readMemory, (R800WriteCb)writeMemory, (R800ReadCb)r800ReadIo, (R800WriteCb)r800WriteIo, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    r800Reset(cpu, 0);
    if (romData) {
        if (romMapper == MAPPER_NONE) {
            bool mapFromZero = false;
            // Heuristic for plain ROMs that start at 0x0000
            if (romSize <= 0x4000 && !(romData[0] == 'A' && romData[1] == 'B')) {
                mapFromZero = true;
            }

            if (mapFromZero) {
                if (romSize <= 0x4000) { // <= 16KB at 0x0000
                    primarySlot = 0xFD; // P0=ROM, P1-3=RAM
                } else { // <= 32KB at 0x0000
                    primarySlot = 0xF5; // P0,P1=ROM, P2-3=RAM
                }
            } else { // Standard ROM at 0x4000
                primarySlot = 0xD4;
            }
        } else { // Mapped ROMs
            primarySlot = 0xD4; // Most mappers use 0x4000-0xBFFF
        }
    } else {
        primarySlot = 0x00;
    }
    printf("startEmulator: primarySlot set to 0x%02X\n", primarySlot); fflush(stdout);
}

static std::vector<std::string> scanDirectory(const char* path) {
    std::vector<std::string> files; DIR* dir = opendir(path); if (!dir) return files;
    struct dirent* ent; while ((ent = readdir(dir)) != NULL) {
        std::string name = ent->d_name; std::string low = name; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find(".rom") != std::string::npos || low.find(".zip") != std::string::npos || low.find(".mx1") != std::string::npos) files.push_back(name);
    }
    closedir(dir); std::sort(files.begin(), files.end()); return files;
}

static void saveLastGame(const char* filename) { FILE* f = fopen("last_game.txt", "w"); if (f) { fprintf(f, "%s", filename); fclose(f); } }
static std::string loadLastGame() { char buf[256]; FILE* f = fopen("last_game.txt", "r"); if (!f) return ""; if (fgets(buf, 256, f)) { fclose(f); return std::string(buf); } fclose(f); return ""; }

extern "C" void saveVramSc2(const char* filename) {
    UInt8* vram = vdpGetVramPtr(); if (!vram) return;
    FILE* f = fopen(filename, "wb"); if (!f) return;
    UInt8 h[7] = { 0xFE, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00 }; fwrite(h, 1, 7, f); fwrite(vram, 1, 0x4000, f); fclose(f);
}

void handleHLE(R800* cpu);

int main(int argc, char* argv[]) {
    const char* targetPath = "."; bool pathIsFile = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) debugMode = true; else if (strcmp(argv[i], "-v") == 0) vramViewerMode = true; else targetPath = argv[i];
    }
    struct stat st; if (stat(targetPath, &st) == 0 && S_ISREG(st.st_mode)) pathIsFile = true;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0) return 1;
    initVideo(); initSound();
    std::vector<std::string> romFiles; int menuSel = 0, menuOff = 0; std::string baseDir = targetPath;
    if (pathIsFile) { if (loadRom(targetPath)) { startEmulator(); appState = STATE_EMU; } }
    else {
        romFiles = scanDirectory(targetPath);
        if (romFiles.empty()) appState = STATE_EMU;
        else {
            appState = STATE_MENU;
            std::string last = loadLastGame();
            if (!last.empty()) {
                auto it = std::find(romFiles.begin(), romFiles.end(), last);
                if (it != romFiles.end()) { menuSel = std::distance(romFiles.begin(), it); menuOff = (menuSel / 24) * 24; }
            }
        }
    }
    bool quit = false, fullscreen = false; SDL_Event e; Uint32 lastTime = SDL_GetTicks();
    printf("main: Entering main loop.\n"); fflush(stdout);
    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    fullscreen = !fullscreen; SDL_SetWindowFullscreen(getMainWindow(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                if (appState == STATE_MENU) {
                    int pageSize = 24;
                    if (e.key.keysym.sym == SDLK_UP) menuSel--; else if (e.key.keysym.sym == SDLK_DOWN) menuSel++;
                    else if (e.key.keysym.sym == SDLK_PAGEUP || e.key.keysym.sym == SDLK_LEFT) menuSel -= pageSize;
                    else if (e.key.keysym.sym == SDLK_PAGEDOWN || e.key.keysym.sym == SDLK_RIGHT) menuSel += pageSize;
                    else if (e.key.keysym.sym == SDLK_RETURN) {
                        std::string full = baseDir + "/" + romFiles[menuSel];
                        if (loadRom(full.c_str())) { saveLastGame(romFiles[menuSel].c_str()); startEmulator(); appState = STATE_EMU; }
                    } else if (e.key.keysym.sym == SDLK_c && (e.key.keysym.mod & KMOD_CTRL)) {
                        if (!romFiles.empty() && menuSel >= 0 && menuSel < romFiles.size()) {
                            std::string full = baseDir + "/" + romFiles[menuSel];
#ifdef _WIN32
                            char resolved_path[MAX_PATH];
                            if (_fullpath(resolved_path, full.c_str(), MAX_PATH)) {
                                SDL_SetClipboardText(resolved_path);
                            }
#else
                            char resolved_path[PATH_MAX];
                            if (realpath(full.c_str(), resolved_path)) {
                                SDL_SetClipboardText(resolved_path);
                            }
#endif
                        }
                    }
                    if (menuSel < 0) menuSel = 0; if (menuSel >= (int)romFiles.size()) menuSel = (int)romFiles.size() - 1;
                    if (menuSel < menuOff) menuOff = menuSel; if (menuSel >= menuOff + pageSize) menuOff = menuSel - pageSize + 1;
                } else {
                    if (e.key.keysym.sym == SDLK_F7) { if (!romFiles.empty()) appState = STATE_MENU; else startEmulator(); }
                    if (e.key.keysym.sym == SDLK_F8) scanlinesEnabled = !scanlinesEnabled;
                    if (e.key.keysym.sym == SDLK_PRINTSCREEN) { saveVramSc2("capture.sc2"); saveScreenshot("capture.bmp"); }
                }
            }
        }
        if (appState == STATE_EMU) {
            updateKeyboard();
            Uint32 now = SDL_GetTicks();
            if ((Int32)(now - lastTime) >= 16) {
                for (int i = 0; i < 262; i++) {
                    currentBoardTime += 1368; r800ExecuteUntil(cpu, currentBoardTime);
                    handleHLE(cpu); 
                    while (updateTimers(currentBoardTime));
                }
                RefreshScreen(0); updateSound(); lastTime = now;
            } else SDL_Delay(1);
        } else { DrawMenu(romFiles, menuSel, menuOff); SDL_Delay(16); }
    }
    SDL_Quit(); return 0;
}

void handleHLE(R800* cpu) {
    if (cpu->regs.PC.W == 0x005C) {
        UInt16 count = cpu->regs.BC.W, dest = cpu->regs.DE.W, src = cpu->regs.HL.W;
        writeIoPort(NULL, 0x99, dest & 0xFF); writeIoPort(NULL, 0x99, (dest >> 8) | 0x40);
        for (UInt16 i = 0; i < count; i++) writeIoPort(NULL, 0x98, readMemory(NULL, src + i));
        cpu->regs.HL.W += count; cpu->regs.DE.W += count; cpu->regs.BC.W = 0;
        UInt16 sp = cpu->regs.SP.W; cpu->regs.PC.W = readMemory(NULL, sp) | (readMemory(NULL, sp + 1) << 8); cpu->regs.SP.W = sp + 2;
    }
}
