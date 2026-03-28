#include "msxplay.h"
#include <SDL2/SDL_gamecontroller.h>
#include "hash_util.h"
#include "mapper_db.h"
#include "bios_loader.h"
#include "FrameBuffer.h"
#include "unzip.h"
#include "bios_data.h"
extern "C" {
#include "SCC.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <cstring>
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
    void ay8910Reset(AY8910*);
    void ay8910WriteAddress(AY8910*, UInt16, UInt8);
    void ay8910WriteData(AY8910*, UInt16, UInt8);
    UInt8 ay8910ReadData(AY8910*, UInt16);
    void ay8910SetIoPort(AY8910* ay8910, AY8910ReadCb readCb, AY8910ReadCb pollCb, AY8910WriteCb writeCb, void* arg);
}

extern void initVideo();
extern void initSound();
extern "C" void RefreshScreen(int);
extern "C" void DrawMenu(const std::vector<std::string>& files, int selected, int offset, int biosMode);
extern "C" void* ioPortGetRef(int port);
extern "C" void saveScreenshot(const char* filename);
extern "C" void updateSound();
extern "C" void clearQueuedAudio(void);
extern "C" SDL_Window* getMainWindow();

bool debugMode = false;
bool vramViewerMode = false;
bool scanlinesEnabled = true;
UInt8 primarySlot = 0x00;
static AY8910* psg = NULL;
static SCC* g_scc = NULL;
static int g_konamiSccRegEnabled = 0;
static bool vdpCreated = false;
static MapperDb g_mapperDb;
static bool g_mapperDbTriedLoad = false;

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

void startEmulator();

static const char* kMapperDbPath = "mapper_db.csv";

/** ASCII8SRAM2 (openMSX RomAscii8_8 / ASCII8_2): 2 KiB SRAM at 0x8000–0xBFFF when enabled. */
static const size_t kAscii8Sram2Size = 0x800;
static UInt8 g_a8s2[kAscii8Sram2Size];
static UInt8 g_a8s2_enable; /* bits: Z80 8 KiB region index (address>>13) */
static UInt8 g_a8s2_block[8];

static void resetAscii8Sram2Storage(void) {
    memset(g_a8s2, 0, sizeof(g_a8s2));
    g_a8s2_enable = 0;
    memset(g_a8s2_block, 0, sizeof(g_a8s2_block));
}

/** openMSX RomAscii16_2 (2 KiB SRAM); mirrored in each 16 KiB page when enabled via bank write 0x10. */
static const size_t kAscii16Sram2Size = 0x800;
static UInt8 g_a16s2[kAscii16Sram2Size];
/** Bits match openMSX: (1<<region) for region 1..2 from bank port (4000h / 8000h 16K windows). */
static UInt8 g_a16s2_enable;

static void resetAscii16Sram2Storage(void) {
    memset(g_a16s2, 0, sizeof(g_a16s2));
    g_a16s2_enable = 0;
}

/** Menu defaults (also used when ROM has no DB row). See bios_loader.h for mode IDs. */
static unsigned char menuBiosMode = 0;
/** Active session: applied in startEmulator and saved with F12. */
static unsigned char g_biosMode = 0;
static std::string g_romSearchBase = ".";
/** Per-ROM SHA1: DB bios row applied only on first load with a row; later loads use menu (B). */
static std::unordered_set<std::string> g_basicFontDbHydratedSha1;

/** B key cycles BIOS in this order (actual biosMode values). */
static const unsigned char kBiosCycleOrder[] = { 0, 1, 5, 2, 3, 4 };

static void menuAdvanceBiosMode(void) {
    const int n = (int)(sizeof(kBiosCycleOrder) / sizeof(kBiosCycleOrder[0]));
    int i = 0;
    for (; i < n; i++) {
        if (kBiosCycleOrder[i] == menuBiosMode) break;
    }
    if (i >= n) i = 0;
    i = (i + 1) % n;
    menuBiosMode = kBiosCycleOrder[i];
}

static void applyMenuOrDbBasicFont(const std::string& sha1, bool haveProf, const RomDbProfile& prof) {
    if (haveProf) {
        if (g_basicFontDbHydratedSha1.find(sha1) == g_basicFontDbHydratedSha1.end()) {
            g_basicFontDbHydratedSha1.insert(sha1);
            unsigned char bm = prof.biosMode;
            /* Legacy mapper_db: bios=1 + font j/k meant Japanese C-BIOS main ROM */
            if (bm == 1 && (prof.font == 'j' || prof.font == 'J' || prof.font == 'k' || prof.font == 'K'))
                bm = 5;
            if (bm > 5) bm = 0;
            g_biosMode = bm;
            menuBiosMode = bm;
        } else {
            g_biosMode = menuBiosMode;
        }
    } else {
        g_biosMode = menuBiosMode;
    }
}

static void applyMegaRomHeuristic() {
    int kWrite = 0;
    int ascii8Write = 0;
    int ascii16Write = 0;
    for (int i = 0; i + 2 < romSize; ++i) {
        if (romData[i] == 0x32) { // LD (nn),A
            UInt16 a = (UInt16)romData[i + 1] | ((UInt16)romData[i + 2] << 8);
            if (a == 0x6000 || a == 0x8000 || a == 0xA000) kWrite++;
            if (a >= 0x6000 && a < 0x8000) ascii8Write++;
            if (a == 0x6000 || a == 0x7000) ascii16Write++;
        }
    }
    if (ascii8Write > kWrite + 2 && ascii8Write >= ascii16Write) romMapper = MAPPER_ASCII8;
    else if (ascii16Write > kWrite + 2) romMapper = MAPPER_ASCII16;
    else romMapper = MAPPER_KONAMI;
}

/** openMSX RomPlain MIRRORED: 8 KiB pages across full 64 KiB; pages past ROM end use mask/modulo (openMSX). */
static unsigned g_mirroredFirstPage = 2; /* Z80 page index 0..7 (2 = base 0x4000) */

static void computeMirroredFirstPage(void) {
    g_mirroredFirstPage = 2;
    if (!romData || romSize < 0x10) return;
    auto guessHelper = [](UInt8* d, int rs, unsigned off, int* p0, int* p1, int* p2) {
        if (off + 0x10 > (unsigned)rs) return;
        if (d[off] != 'A' || d[off + 1] != 'B') return;
        for (int i = 0; i < 4; ++i) {
            UInt16 addr = (UInt16)d[off + 2 + 2 * i] | ((UInt16)d[off + 2 + 2 * i + 1] << 8);
            if (addr != 0) {
                unsigned pg = (unsigned)(addr >> 14) - (off >> 14);
                if (pg == 0) (*p0)++;
                else if (pg == 1) (*p1)++;
                else if (pg == 2) (*p2)++;
            }
        }
    };
    int p0 = 0, p1 = 0, p2 = 0;
    guessHelper(romData, romSize, 0x0000, &p0, &p1, &p2);
    if (romSize >= 0x4010) guessHelper(romData, romSize, 0x4000, &p0, &p1, &p2);
    if (p1 && p1 >= p0 && p1 >= p2) g_mirroredFirstPage = 2;
    else if (p0 && p0 >= p2) g_mirroredFirstPage = 0;
    else if (p2) g_mirroredFirstPage = 4;
}

static UInt8 readMirroredRomAt(UInt16 address) {
    if (!romData) return 0xFF;
    unsigned region = (unsigned)address >> 13;
    unsigned firstPage = g_mirroredFirstPage;
    unsigned num8k = (unsigned)(romSize / 0x2000);
    if (num8k < 1) num8k = 1;
    unsigned romPage = region - firstPage;
    unsigned block;
    if (romPage < num8k)
        block = romPage;
    else if ((num8k & (num8k - 1)) == 0)
        block = romPage & (num8k - 1);
    else
        block = romPage % num8k;
    unsigned off = block * 0x2000u + (address & 0x1FFFu);
    if (off < (unsigned)romSize) return romData[off];
    return 0xFF;
}

static void detectMapper() {
    romMapper = MAPPER_NONE;
    resetAscii8Sram2Storage();
    resetAscii16Sram2Storage();

    if (!g_mapperDbTriedLoad) {
        g_mapperDb.load(kMapperDbPath);
        g_mapperDbTriedLoad = true;
    }

    const std::string sha1 = sha1Hex(romData, romSize);
    RomDbProfile prof;
    const bool haveProf = g_mapperDb.findProfile(sha1, prof);

    if (romSize < 0x10000) {
        if (haveProf && prof.mapper != MAPPER_NONE)
            romMapper = prof.mapper;
        else if (romSize <= 0x4000 && romData[0] == 'A' && romData[1] == 'B') {
            /* openMSX RomFactory: PAGE2 cartridge (e.g. Exchanger) — ROM at 8000h only */
            UInt16 initAddr = (UInt16)romData[2] | ((UInt16)romData[3] << 8);
            UInt16 textAddr = (UInt16)romData[8] | ((UInt16)romData[9] << 8);
            if ((textAddr & 0xC000) == 0x8000) {
                if (initAddr == 0 ||
                    (((initAddr & 0xC000) == 0x8000) &&
                     romSize > 0 && romData[initAddr & (romSize - 1)] == 0xC9))
                    romMapper = MAPPER_PAGE2;
            }
        }
        /* openMSX RomFactory::guessRomType: ROM < 64K and not PAGE2 -> Mirrored (wrap past ROM in cart window). */
        if (romMapper == MAPPER_NONE && !(haveProf && prof.mapper == MAPPER_NONE))
            romMapper = MAPPER_MIRRORED;

        applyMenuOrDbBasicFont(sha1, haveProf, prof);
        romBanks[0] = 0;
        romBanks[1] = 1;
        romBanks[2] = 2;
        romBanks[3] = 3;
        if (romMapper != MAPPER_NONE)
            printf("detectMapper: <64KiB ROM %s -> %s\n", sha1.c_str(), mapperTypeName(romMapper));
        if (romMapper == MAPPER_MIRRORED) computeMirroredFirstPage();
        fflush(stdout);
        return;
    }

    if (haveProf && prof.mapper != MAPPER_NONE) {
        romMapper = prof.mapper;
    } else {
        applyMegaRomHeuristic();
        if (haveProf)
            printf("detectMapper: DB mapper NONE for mega ROM %s -> heuristic %s\n", sha1.c_str(), mapperTypeName(romMapper));
        else
            printf("detectMapper: SHA1 DB miss %s -> heuristic %s\n", sha1.c_str(), mapperTypeName(romMapper));
        fflush(stdout);
    }

    applyMenuOrDbBasicFont(sha1, haveProf, prof);

    if (haveProf && prof.mapper != MAPPER_NONE) {
        printf("detectMapper: SHA1 DB hit %s -> %s bios=%u\n", sha1.c_str(), mapperTypeName(romMapper),
               (unsigned)menuBiosMode);
        fflush(stdout);
    }

    romBanks[0] = 0;
    romBanks[1] = 1;
    romBanks[2] = 2;
    romBanks[3] = 3;
    /* openMSX RomAscii16kB::reset: both 16K windows start on ROM block 0 (not 0+1). */
    if (romMapper == MAPPER_ASCII16 || romMapper == MAPPER_ASCII16_SRAM2 || romMapper == MAPPER_MSXWRITE)
        romBanks[1] = 0;
    /* openMSX RomAscii8kB / RomAscii8_8::reset: all four 8K windows map to ROM block 0. */
    if (romMapper == MAPPER_ASCII8 || romMapper == MAPPER_ASCII8_SRAM2)
        romBanks[0] = romBanks[1] = romBanks[2] = romBanks[3] = 0;
    if (romMapper == MAPPER_RTYPE)
        romBanks[0] = 0;
    if (romMapper == MAPPER_MIRRORED) computeMirroredFirstPage();
}

static void cycleMapperAndSoftReset() {
    if (!romData) return;

    static const MapperType orderMega[] = {
        MAPPER_KONAMI, MAPPER_KONAMI_SCC, MAPPER_ASCII8, MAPPER_ASCII8_SRAM2, MAPPER_ASCII16, MAPPER_ASCII16_SRAM2,
        MAPPER_RTYPE, MAPPER_MIRRORED
    };
    static const MapperType orderSmall[] = { MAPPER_NONE, MAPPER_PAGE2, MAPPER_MIRRORED };

    const MapperType* order;
    int n;
    if (romSize < 0x10000) {
        order = orderSmall;
        n = (int)(sizeof(orderSmall) / sizeof(orderSmall[0]));
    } else {
        order = orderMega;
        n = (int)(sizeof(orderMega) / sizeof(orderMega[0]));
    }
    int idx = -1;
    for (int i = 0; i < n; ++i) {
        if (order[i] == romMapper) {
            idx = i;
            break;
        }
    }
    idx = (idx + 1) % n;
    romMapper = order[idx];
    romBanks[0] = 0;
    romBanks[1] = 1;
    romBanks[2] = 2;
    romBanks[3] = 3;
    if (romMapper == MAPPER_ASCII16 || romMapper == MAPPER_ASCII16_SRAM2 || romMapper == MAPPER_MSXWRITE)
        romBanks[1] = 0;
    if (romMapper == MAPPER_ASCII8 || romMapper == MAPPER_ASCII8_SRAM2)
        romBanks[0] = romBanks[1] = romBanks[2] = romBanks[3] = 0;
    if (romMapper == MAPPER_RTYPE)
        romBanks[0] = 0;
    if (romMapper == MAPPER_MIRRORED) computeMirroredFirstPage();
    printf("cycleMapper+reset: mapper=%s (Ctrl+F5 cycles)\n", mapperTypeName(romMapper));
    fflush(stdout);
    startEmulator();
}

bool loadRom(const char* filename) {
    if (romData) { free(romData); romData = NULL; }
    if (strstr(filename, ".zip") || strstr(filename, ".ZIP")) {
        unzFile uf = unzOpen(filename);
        if (!uf) return false;
        struct ZipRomCand {
            std::string name;
            uLong usize;
        };
        std::vector<ZipRomCand> zipCands;
        if (unzGoToFirstFile(uf) != UNZ_OK) {
            unzClose(uf);
            return false;
        }
        do {
            unz_file_info info;
            char name[512];
            if (unzGetCurrentFileInfo(uf, &info, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK) break;
            std::string low = name;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            bool extOk = low.find(".rom") != std::string::npos || low.find(".mx1") != std::string::npos ||
                         low.find(".bin") != std::string::npos;
            if (extOk && info.uncompressed_size > 0)
                zipCands.push_back(ZipRomCand{std::string(name), info.uncompressed_size});
        } while (unzGoToNextFile(uf) == UNZ_OK);
        if (zipCands.empty()) {
            unzClose(uf);
            return false;
        }
        auto bestIt = std::max_element(zipCands.begin(), zipCands.end(),
            [](const ZipRomCand& a, const ZipRomCand& b) { return a.usize < b.usize; });
        if (unzLocateFile(uf, bestIt->name.c_str(), 2) != UNZ_OK) {
            unzClose(uf);
            return false;
        }
        romSize = (int)bestIt->usize;
        if (unzOpenCurrentFile(uf) != UNZ_OK) {
            unzClose(uf);
            return false;
        }
        romData = (UInt8*)malloc(romSize);
        int zread = unzReadCurrentFile(uf, romData, romSize);
        unzCloseCurrentFile(uf);
        unzClose(uf);
        if (zread != romSize) {
            free(romData);
            romData = NULL;
            return false;
        }
        detectMapper();
        {
            std::string fn = filename;
            size_t slash = fn.find_last_of("/\\");
            g_romSearchBase = (slash == std::string::npos) ? std::string(".") : fn.substr(0, slash);
        }
        printf("loadRom: ZIP '%s' -> inner '%s' size=%d mapper=%s\n", filename, bestIt->name.c_str(), romSize,
               mapperTypeName(romMapper));
        fflush(stdout);
        return true;
    } else {
        FILE* f = fopen(filename, "rb"); if (!f) return false;
        fseek(f, 0, SEEK_END); romSize = ftell(f); fseek(f, 0, SEEK_SET);
        romData = (UInt8*)malloc(romSize);
        size_t nread = fread(romData, 1, (size_t)romSize, f);
        fclose(f);
        if (nread != (size_t)romSize) {
            free(romData);
            romData = NULL;
            return false;
        }
        detectMapper();
        {
            std::string fn = filename;
            size_t slash = fn.find_last_of("/\\");
            g_romSearchBase = (slash == std::string::npos) ? std::string(".") : fn.substr(0, slash);
        }
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
        if (romMapper == MAPPER_PAGE2) {
            if (address >= 0x8000 && address < 0xC000) {
                unsigned off = (unsigned)(address - 0x8000);
                if (off < 0x4000u && off < (unsigned)romSize) return romData[off];
            }
            return 0xFF;
        }
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
        } else if (romMapper == MAPPER_MIRRORED) {
            return readMirroredRomAt(address);
        } else if (romMapper == MAPPER_RTYPE) {
            /* openMSX RomRType: 4000h–7FFFh fixed 16K bank 0x0f/0x17; 8000h–BFFFh switched by writes to 4000h–7FFFh */
            if (address >= 0x4000 && address < 0xC000) {
                int nb16 = romSize / 0x4000;
                if (nb16 < 1) nb16 = 1;
                int bank = (address < 0x8000) ? 0x17 : romBanks[0];
                bank = (int)((unsigned)bank % (unsigned)nb16);
                int offset = bank * 0x4000 + (address & 0x3FFF);
                if (offset < romSize) return romData[offset];
            }
            return 0xFF;
        } else {
            // Konami, Konami SCC, ASCII8 / ASCII8SRAM2 (8KB banks), ASCII16 (16KB banks)
            if (address >= 0x4000 && address < 0xC000) {
                if (romMapper == MAPPER_KONAMI_SCC && g_konamiSccRegEnabled && address >= 0x9800 &&
                    address < 0xA000 && g_scc)
                    return sccRead(g_scc, (UInt8)(address & 0xFF));
                if (romMapper == MAPPER_ASCII8_SRAM2) {
                    unsigned zbank = (unsigned)address >> 13;
                    if (g_a8s2_enable & (1u << zbank)) {
                        unsigned off = (unsigned)g_a8s2_block[zbank] * 0x2000u;
                        off += ((unsigned)address & 0x1FFFu) & (kAscii8Sram2Size - 1);
                        if (off < kAscii8Sram2Size) return g_a8s2[off];
                        return 0xFF;
                    }
                }
                int offset = 0;
                if (romMapper == MAPPER_ASCII16_SRAM2) {
                    unsigned apg = (unsigned)address >> 14;
                    if ((g_a16s2_enable & (UInt8)(1u << apg)) != 0)
                        return g_a16s2[address & (kAscii16Sram2Size - 1)];
                }
                if (romMapper == MAPPER_ASCII16 || romMapper == MAPPER_ASCII16_SRAM2 || romMapper == MAPPER_MSXWRITE) {
                    int bankIdx16 = (address < 0x8000) ? 0 : 1;
                    int bank = romBanks[bankIdx16];
                    offset = (bank * 0x4000) + (address % 0x4000);
                } else {
                    int bankIdx = (address - 0x4000) / 0x2000;
                    int bank = romBanks[bankIdx];
                    offset = (bank * 0x2000) + (address % 0x2000);
                }
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
        if (romMapper == MAPPER_RTYPE) {
            if (address >= 0x4000 && address < 0x8000) {
                int nb16 = romSize / 0x4000;
                if (nb16 < 1) nb16 = 1;
                UInt8 v = value;
                v &= (UInt8)((v & 0x10) ? 0x17 : 0x1F);
                romBanks[0] = (int)((unsigned)v % (unsigned)nb16);
                return;
            }
        } else if (romMapper == MAPPER_KONAMI_SCC) {
            /* openMSX RomKonamiSCC: banks 5000/7000/9000/B000; SCC @9800; enable via 9000–97FF */
            if (address >= 0x5000 && address < 0xC000) {
                if (g_konamiSccRegEnabled && address >= 0x9800 && address < 0xA000 && g_scc) {
                    sccWrite(g_scc, (UInt8)(address & 0xFF), value);
                    return;
                }
                /* openMSX: 9000h–97FFh updates SCC enable then may still fall through to
                 * bank select — 9000h matches (addr&0x1800)==1000h (region 4), so one write
                 * can both flip SCC and set the 8000h-page bank. Do not return early. */
                if ((address & 0xF800) == 0x9000)
                    g_konamiSccRegEnabled = ((value & 0x3F) == 0x3F) ? 1 : 0;
                if ((address & 0x1800) == 0x1000) {
                    int region = address >> 13;
                    if (region >= 2 && region <= 5) {
                        int nb8 = romSize / 0x2000;
                        if (nb8 < 1) nb8 = 1;
                        romBanks[region - 2] = (int)((unsigned)value % (unsigned)nb8);
                    }
                    return;
                }
                return;
            }
        } else if (romMapper == MAPPER_KONAMI) {
            if (address == 0x6000 || address == 0x8000 || address == 0xA000) {
                 int nb8 = romSize / 0x2000;
                 if (nb8 < 1) nb8 = 1;
                 if (address == 0x6000) romBanks[1] = value % nb8;
                 else if (address == 0x8000) romBanks[2] = value % nb8;
                 else if (address == 0xA000) romBanks[3] = value % nb8;
                 return;
            }
        } else if (romMapper == MAPPER_ASCII16) {
            /* openMSX RomAscii16kB: only 6000–67FF and 7000–77FF; 6800–6FFF (bit 0x800) ignored */
            if (address >= 0x6000 && address < 0x7800 && (address & 0x0800) == 0) {
                int nb16 = romSize / 0x4000;
                if (nb16 < 1) nb16 = 1;
                if (address < 0x7000) romBanks[0] = value % nb16;
                else romBanks[1] = value % nb16;
                return;
            }
        } else if (romMapper == MAPPER_MSXWRITE) {
            /* openMSX RomMSXWrite: ASCII16 ports + 6FFFh (bank 4000h) and 7FFFh (bank 8000h) */
            if ((address >= 0x6000 && address < 0x7800 && (address & 0x0800) == 0) || address == 0x6FFF ||
                address == 0x7FFF) {
                int nb16 = romSize / 0x4000;
                if (nb16 < 1) nb16 = 1;
                if (address < 0x7000) romBanks[0] = value % nb16;
                else romBanks[1] = value % nb16;
                return;
            }
        } else if (romMapper == MAPPER_ASCII16_SRAM2) {
            /* openMSX RomAscii16_2: same ports; value 0x10 selects SRAM for that 16K region (read both pages; write 8000h only). */
            if (address >= 0x6000 && address < 0x7800 && (address & 0x0800) == 0) {
                int nb16 = romSize / 0x4000;
                if (nb16 < 1) nb16 = 1;
                int region = ((address >> 12) & 1) + 1;
                if (value == 0x10) {
                    g_a16s2_enable |= (UInt8)(1 << region);
                } else {
                    g_a16s2_enable &= (UInt8)~(1 << region);
                    if (address < 0x7000) romBanks[0] = value % nb16;
                    else romBanks[1] = value % nb16;
                }
                return;
            }
        } else if (romMapper == MAPPER_ASCII8 || romMapper == MAPPER_ASCII8_SRAM2) {
            if (address >= 0x6000 && address < 0x8000) {
                int nb8 = romSize / 0x2000;
                if (nb8 < 1) nb8 = 1;
                int reg = (address >> 11) & 3;
                if (romMapper == MAPPER_ASCII8_SRAM2) {
                    /* openMSX RomAscii8_8 SubType::ASCII8_2 */
                    UInt8 sramBit = (UInt8)((unsigned)romSize / 0x2000u);
                    if (sramBit == 0) sramBit = 1;
                    int region = reg + 2;
                    const UInt8 sramPages = 0x30; /* only 0x8000–0xBFFF */
                    UInt8 blkMask = (UInt8)((kAscii8Sram2Size + 0x1FFFu) / 0x2000u - 1);
                    if (value & sramBit) {
                        g_a8s2_enable |= (UInt8)((1 << region) & sramPages);
                        g_a8s2_block[region] = (UInt8)(value & blkMask);
                    } else {
                        g_a8s2_enable &= (UInt8)~(1 << region);
                        romBanks[reg] = (int)((unsigned)value % (unsigned)nb8);
                    }
                } else {
                    int v = (int)((unsigned)value % (unsigned)nb8);
                    if (address >= 0x6000 && address < 0x6800) romBanks[0] = v;
                    else if (address >= 0x6800 && address < 0x7000) romBanks[1] = v;
                    else if (address >= 0x7000 && address < 0x7800) romBanks[2] = v;
                    else if (address >= 0x7800 && address < 0x8000) romBanks[3] = v;
                }
                return;
            }
        }
    }

    if ((slot == 1 || slot == 2) && romMapper == MAPPER_ASCII8_SRAM2 && address >= 0x4000 && address < 0xC000 &&
        (address < 0x6000 || address >= 0x8000)) {
        unsigned zbank = (unsigned)address >> 13;
        if (g_a8s2_enable & (1u << zbank)) {
            unsigned off = (unsigned)g_a8s2_block[zbank] * 0x2000u;
            off += ((unsigned)address & 0x1FFFu) & (kAscii8Sram2Size - 1);
            if (off < kAscii8Sram2Size) g_a8s2[off] = value;
            return;
        }
    }

    if ((slot == 1 || slot == 2) && romMapper == MAPPER_ASCII16_SRAM2 && address >= 0x4000 && address < 0xC000) {
        /* openMSX: (1<<(addr>>14)) & sramEnabled & 0x04 — only 8000h–BFFF SRAM is writable */
        unsigned apg = (unsigned)address >> 14;
        if (((1u << apg) & (unsigned)g_a16s2_enable & 0x04u) != 0) {
            g_a16s2[address & (kAscii16Sram2Size - 1)] = value;
            return;
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

static SDL_GameController* g_gameController = NULL;

/** Embed SDL_GameControllerDB rows for 8BitDo Micro (incl. Android mode GUIDs) so SDL_IsGameController succeeds without an external db file. */
static void register8BitDoMicroMappings(void) {
    static const char* const rows[] = {
        "03000000c82d00002090000000000000,8BitDo Micro,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,righttrigger:b9,rightx:a3,righty:a4,start:b11,x:b4,y:b3,platform:Windows",
        "03000000c82d00002090000000010000,8BitDo Micro,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,lefttrigger:a5,leftx:a0,lefty:a1,rightshoulder:b7,righttrigger:a4,rightx:a2,righty:a3,start:b11,x:b4,y:b3,platform:Mac OS X",
        "03000000c82d00002090000011010000,8BitDo Micro,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,righttrigger:b9,rightx:a2,righty:a3,start:b11,x:b4,y:b3,platform:Linux",
        "05000000c82d00002090000000010000,8BitDo Micro,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,righttrigger:b9,rightx:a2,righty:a3,start:b11,x:b4,y:b3,platform:Linux",
        "38426974446f204d6963726f2067616d,8BitDo Micro,a:b1,b:b0,back:b15,dpdown:b12,dpleft:b13,dpright:b14,dpup:b11,guide:b5,leftshoulder:b9,lefttrigger:a4,leftx:b0,lefty:b1,rightshoulder:b10,righttrigger:a5,rightx:b2,righty:b3,start:b6,x:b3,y:b2,platform:Android",
        "61653365323561356263373333643266,8BitDo Micro,a:b1,b:b0,back:b15,dpdown:b12,dpleft:b13,dpright:b14,dpup:b11,guide:b5,leftshoulder:b9,lefttrigger:a4,leftx:b0,lefty:b1,rightshoulder:b10,righttrigger:a5,rightx:b2,righty:b3,start:b6,x:b3,y:b2,platform:Android",
        "62613137616239666338343866326336,8BitDo Micro,a:b1,b:b0,back:b15,dpdown:b12,dpleft:b13,dpright:b14,dpup:b11,guide:b5,leftshoulder:b9,lefttrigger:a4,leftx:b0,lefty:b1,rightshoulder:b10,righttrigger:a5,rightx:b2,righty:b3,start:b6,x:b3,y:b2,platform:Android",
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        SDL_GameControllerAddMapping(rows[i]);
}

static void closeGameController(void) {
    if (g_gameController) {
        SDL_GameControllerClose(g_gameController);
        g_gameController = NULL;
    }
}

static void tryOpenFirstGameController(void) {
    if (g_gameController) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            g_gameController = SDL_GameControllerOpen(i);
            if (g_gameController) break;
        }
    }
}

/** MSX joystick via PSG port A: bits 0–3 U/D/L/R, 4–5 buttons (active low). Merge with keyboard. */
static void applyGamepadToMSXJoystick(UInt8* val) {
    if (!g_gameController || !SDL_GameControllerGetAttached(g_gameController)) return;
    if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_DPAD_UP)) *val &= ~0x01;
    if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) *val &= ~0x02;
    if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) *val &= ~0x04;
    if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) *val &= ~0x08;
    const Sint16 dead = 12000;
    Sint16 ax = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ay = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_LEFTY);
    if (ay < -dead) *val &= ~0x01;
    if (ay > dead) *val &= ~0x02;
    if (ax < -dead) *val &= ~0x04;
    if (ax > dead) *val &= ~0x08;
    if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_A)) *val &= ~0x10;
    if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_B)) *val &= ~0x20;
}

static UInt8 psgPortB = 0;
static UInt8 psgRead(void* arg, UInt16 port) {
    if (port == 0) { // Port A (Joystick)
        UInt8 val = 0x3F; const Uint8* s = SDL_GetKeyboardState(NULL);
        if (!(psgPortB & 0x40)) {
            if (s[SDL_SCANCODE_UP]) val &= ~0x01;
            if (s[SDL_SCANCODE_DOWN]) val &= ~0x02;
            if (s[SDL_SCANCODE_LEFT]) val &= ~0x04;
            if (s[SDL_SCANCODE_RIGHT]) val &= ~0x08;
            if (s[SDL_SCANCODE_SPACE] || s[SDL_SCANCODE_Z]) val &= ~0x10;
            if (s[SDL_SCANCODE_X] || s[SDL_SCANCODE_LSHIFT]) val &= ~0x20;
            applyGamepadToMSXJoystick(&val);
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
    if (s[SDL_SCANCODE_0]) keyMatrix[0] &= ~0x01;
    if (s[SDL_SCANCODE_1]) keyMatrix[0] &= ~0x02;
    if (s[SDL_SCANCODE_2]) keyMatrix[0] &= ~0x04;
    if (s[SDL_SCANCODE_3]) keyMatrix[0] &= ~0x08;
    if (s[SDL_SCANCODE_4]) keyMatrix[0] &= ~0x10;
    if (s[SDL_SCANCODE_5]) keyMatrix[0] &= ~0x20;
    if (s[SDL_SCANCODE_6]) keyMatrix[0] &= ~0x40;
    if (s[SDL_SCANCODE_7]) keyMatrix[0] &= ~0x80;
    if (s[SDL_SCANCODE_8]) keyMatrix[1] &= ~0x01;
    if (s[SDL_SCANCODE_9]) keyMatrix[1] &= ~0x02;
    if (s[SDL_SCANCODE_MINUS]) keyMatrix[1] &= ~0x04;
    if (s[SDL_SCANCODE_EQUALS]) keyMatrix[1] &= ~0x08;
    if (s[SDL_SCANCODE_BACKSLASH]) keyMatrix[1] &= ~0x10;
    if (s[SDL_SCANCODE_LEFTBRACKET]) keyMatrix[1] &= ~0x20;
    if (s[SDL_SCANCODE_RIGHTBRACKET]) keyMatrix[1] &= ~0x40;
    if (s[SDL_SCANCODE_SEMICOLON]) keyMatrix[1] &= ~0x80;
    if (s[SDL_SCANCODE_APOSTROPHE]) keyMatrix[2] &= ~0x01;
    if (s[SDL_SCANCODE_COMMA]) keyMatrix[2] &= ~0x04;
    if (s[SDL_SCANCODE_PERIOD]) keyMatrix[2] &= ~0x08;
    if (s[SDL_SCANCODE_SLASH]) keyMatrix[2] &= ~0x10;
    if (s[SDL_SCANCODE_A]) keyMatrix[2] &= ~0x40;
    if (s[SDL_SCANCODE_B]) keyMatrix[2] &= ~0x80;
    if (s[SDL_SCANCODE_C]) keyMatrix[3] &= ~0x01;
    if (s[SDL_SCANCODE_D]) keyMatrix[3] &= ~0x02;
    if (s[SDL_SCANCODE_E]) keyMatrix[3] &= ~0x04;
    if (s[SDL_SCANCODE_F]) keyMatrix[3] &= ~0x08;
    if (s[SDL_SCANCODE_G]) keyMatrix[3] &= ~0x10;
    if (s[SDL_SCANCODE_H]) keyMatrix[3] &= ~0x20;
    if (s[SDL_SCANCODE_I]) keyMatrix[3] &= ~0x40;
    if (s[SDL_SCANCODE_J]) keyMatrix[3] &= ~0x80;
    if (s[SDL_SCANCODE_K]) keyMatrix[4] &= ~0x01;
    if (s[SDL_SCANCODE_L]) keyMatrix[4] &= ~0x02;
    if (s[SDL_SCANCODE_M]) keyMatrix[4] &= ~0x04;
    if (s[SDL_SCANCODE_N]) keyMatrix[4] &= ~0x08;
    if (s[SDL_SCANCODE_O]) keyMatrix[4] &= ~0x10;
    if (s[SDL_SCANCODE_P]) keyMatrix[4] &= ~0x20;
    if (s[SDL_SCANCODE_Q]) keyMatrix[4] &= ~0x40;
    if (s[SDL_SCANCODE_R]) keyMatrix[4] &= ~0x80;
    if (s[SDL_SCANCODE_S]) keyMatrix[5] &= ~0x01;
    if (s[SDL_SCANCODE_T]) keyMatrix[5] &= ~0x02;
    if (s[SDL_SCANCODE_U]) keyMatrix[5] &= ~0x04;
    if (s[SDL_SCANCODE_V]) keyMatrix[5] &= ~0x08;
    if (s[SDL_SCANCODE_W]) keyMatrix[5] &= ~0x10;
    if (s[SDL_SCANCODE_X]) keyMatrix[5] &= ~0x20;
    if (s[SDL_SCANCODE_Y]) keyMatrix[5] &= ~0x40;
    if (s[SDL_SCANCODE_Z]) keyMatrix[5] &= ~0x80;
    if (s[SDL_SCANCODE_LSHIFT] || s[SDL_SCANCODE_RSHIFT]) keyMatrix[6] &= ~0x01;
    if (s[SDL_SCANCODE_LCTRL] || s[SDL_SCANCODE_RCTRL]) keyMatrix[6] &= ~0x02;
    if (s[SDL_SCANCODE_LALT]) keyMatrix[6] &= ~0x04;
    if (s[SDL_SCANCODE_F1]) keyMatrix[6] &= ~0x20;
    if (s[SDL_SCANCODE_F2]) keyMatrix[6] &= ~0x40;
    if (s[SDL_SCANCODE_F3]) keyMatrix[6] &= ~0x80;
    if (s[SDL_SCANCODE_F4]) keyMatrix[7] &= ~0x01;
    if (s[SDL_SCANCODE_F5]) keyMatrix[7] &= ~0x02;
    if (s[SDL_SCANCODE_ESCAPE]) keyMatrix[7] &= ~0x04;
    if (s[SDL_SCANCODE_TAB]) keyMatrix[7] &= ~0x08;
    if (s[SDL_SCANCODE_BACKSPACE]) keyMatrix[7] &= ~0x20;
    if (s[SDL_SCANCODE_RETURN]) keyMatrix[7] &= ~0x80;
    if (s[SDL_SCANCODE_SPACE]) keyMatrix[8] &= ~0x01;
    if (s[SDL_SCANCODE_HOME]) keyMatrix[8] &= ~0x02;
    if (s[SDL_SCANCODE_INSERT]) keyMatrix[8] &= ~0x04;
    if (s[SDL_SCANCODE_DELETE]) keyMatrix[8] &= ~0x08;
    if (s[SDL_SCANCODE_LEFT]) keyMatrix[8] &= ~0x10;
    if (s[SDL_SCANCODE_UP]) keyMatrix[8] &= ~0x20;
    if (s[SDL_SCANCODE_DOWN]) keyMatrix[8] &= ~0x40;
    if (s[SDL_SCANCODE_RIGHT]) keyMatrix[8] &= ~0x80;
}

/* VRAM + audio only: zeroing VDP regs desyncs core caches; zeroing palette makes every color black. */
static void clearVideoAndAudioOnReset(void) {
    UInt8* vram = vdpGetVramPtr();
    if (vram) memset(vram, 0, 0x4000);
    if (psg) ay8910Reset(psg);
    clearQueuedAudio();
}

void startEmulator() {
    if (!vdpCreated) { vdpCreate(VDP_MSX, VDP_TMS99x8A, VDP_SYNC_60HZ, 1); vdpCreated = true; }
    if (!psg) { psg = ay8910Create(NULL, AY8910_MSX, PSGTYPE_AY8910, 0, NULL); ay8910SetIoPort(psg, (AY8910ReadCb)psgRead, (AY8910ReadCb)psgRead, (AY8910WriteCb)psgWrite, NULL); }
    if (!g_scc) g_scc = sccCreate(NULL);
    sccReset(g_scc);
    g_konamiSccRegEnabled = 0;
    memset(ram, 0, sizeof(ram));
    {
        std::vector<std::string> dirs;
        dirs.push_back(".");
        if (!g_romSearchBase.empty()) {
            dirs.push_back(g_romSearchBase);
            dirs.push_back(g_romSearchBase + "/bios");
            dirs.push_back(g_romSearchBase + "/MSX1");
        }
        dirs.push_back("bios");
        dirs.push_back("MSX1");
        const bool biosFilesOk = biosLoaderApply(g_biosMode, dirs);
        printf("startEmulator: BIOS mode=%u biosLoaderApply=%s\n", (unsigned)g_biosMode,
               biosFilesOk ? "ok" : "failed (using embedded backup)");
        if (romData && !biosFilesOk && g_biosMode != 0)
            printf("startEmulator: warning: mode %u files missing — put hb-10_basic-bios1.rom (mode 4) or C-BIOS set (modes 1/3/5) under bios/ or MSX1/.\n",
                   (unsigned)g_biosMode);
        fflush(stdout);
    }
    ioPortRegister(0xA0, NULL, (IoPortWrite)myPsgWriteAddr, psg); ioPortRegister(0xA1, NULL, (IoPortWrite)ay8910WriteData, psg); ioPortRegister(0xA2, (IoPortRead)ay8910ReadData, NULL, psg);
    ioPortRegister(0xA9, (IoPortRead)keyboardRead, NULL, NULL); ioPortRegister(0xAA, NULL, (IoPortWrite)keyboardWrite, NULL);
    clearVideoAndAudioOnReset();
    if (!cpu) cpu = r800Create(0, (R800ReadCb)readMemory, (R800WriteCb)writeMemory, (R800ReadCb)r800ReadIo, (R800WriteCb)r800WriteIo, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    r800Reset(cpu, 0);
    if (romMapper == MAPPER_ASCII8_SRAM2) g_a8s2_enable = 0;
    if (romMapper == MAPPER_ASCII16_SRAM2) g_a16s2_enable = 0;
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
        } else if (romMapper == MAPPER_PAGE2) {
            /* P0,P1=slot0 BIOS; P2=slot1 cart @8000h; P3=RAM (openMSX PAGE2 layout) */
            primarySlot = 0xD0;
        } else { // Mapped ROMs at 4000h
            primarySlot = 0xD4;
        }
    } else {
        primarySlot = 0x00;
    }
    printf("startEmulator: primarySlot=0x%02X (CPU reset PC=0000h → BIOS entry; 4000h–BFFF cart when megaROM)\n", primarySlot);
    fflush(stdout);
    RefreshScreen(0);
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

static void menuClampSelection(int& menuSel, int& menuOff, int nFiles, int pageSize) {
    if (nFiles <= 0) return;
    if (menuSel < 0) menuSel = 0;
    if (menuSel >= nFiles) menuSel = nFiles - 1;
    if (menuSel < menuOff) menuOff = menuSel;
    if (menuSel >= menuOff + pageSize) menuOff = menuSel - pageSize + 1;
}

static bool g_menuPadPrev[SDL_CONTROLLER_BUTTON_MAX];
static int g_menuNavDir = 0;
static Uint32 g_menuNavNext = 0;

static void menuResetGamepadNavState(void) {
    g_menuNavDir = 0;
    g_menuNavNext = 0;
    memset(g_menuPadPrev, 0, sizeof(g_menuPadPrev));
}

static void menuUpdateGamepad(std::vector<std::string>& romFiles, int& menuSel, int& menuOff,
    const std::string& baseDir, bool& quit, AppState& appState) {
    if (!g_gameController || romFiles.empty()) return;
    SDL_GameController* gc = g_gameController;
    const int pageSize = MSXPLAY_MENU_PAGE_SIZE;

    bool cur[SDL_CONTROLLER_BUTTON_MAX];
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
        cur[b] = SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b) != 0;

    auto edge = [&](SDL_GameControllerButton btn) { return cur[btn] && !g_menuPadPrev[btn]; };

    /* D-pad or left stick: immediate step + hold repeat */
    int dir = 0;
    if (cur[SDL_CONTROLLER_BUTTON_DPAD_UP]) dir = 1;
    else if (cur[SDL_CONTROLLER_BUTTON_DPAD_DOWN]) dir = 2;
    else if (cur[SDL_CONTROLLER_BUTTON_DPAD_LEFT]) dir = 3;
    else if (cur[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]) dir = 4;
    else {
        const Sint16 dead = 16000;
        Sint16 ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (ay < -dead) dir = 1;
        else if (ay > dead) dir = 2;
        else if (ax < -dead) dir = 3;
        else if (ax > dead) dir = 4;
    }
    Uint32 now = SDL_GetTicks();
    if (dir == 0) {
        g_menuNavDir = 0;
        g_menuNavNext = 0;
    } else if (dir != g_menuNavDir) {
        g_menuNavDir = dir;
        g_menuNavNext = now + 280;
        if (dir == 1) menuSel--;
        else if (dir == 2) menuSel++;
        else if (dir == 3) menuSel -= pageSize;
        else if (dir == 4) menuSel += pageSize;
        menuClampSelection(menuSel, menuOff, (int)romFiles.size(), pageSize);
    } else if (g_menuNavNext != 0 && (Int32)(now - g_menuNavNext) >= 0) {
        g_menuNavNext = now + 90;
        if (dir == 1) menuSel--;
        else if (dir == 2) menuSel++;
        else if (dir == 3) menuSel -= pageSize;
        else if (dir == 4) menuSel += pageSize;
        menuClampSelection(menuSel, menuOff, (int)romFiles.size(), pageSize);
    }

    if (edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
        menuSel -= pageSize;
        menuClampSelection(menuSel, menuOff, (int)romFiles.size(), pageSize);
    }
    if (edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
        menuSel += pageSize;
        menuClampSelection(menuSel, menuOff, (int)romFiles.size(), pageSize);
    }

    if (edge(SDL_CONTROLLER_BUTTON_A) || edge(SDL_CONTROLLER_BUTTON_START)) {
        std::string full = baseDir + "/" + romFiles[menuSel];
        if (loadRom(full.c_str())) {
            saveLastGame(romFiles[menuSel].c_str());
            startEmulator();
            appState = STATE_EMU;
        }
    }
    if (edge(SDL_CONTROLLER_BUTTON_B))
        menuAdvanceBiosMode();
    if (edge(SDL_CONTROLLER_BUTTON_X)) {
        std::string full = baseDir + "/" + romFiles[menuSel];
#ifdef _WIN32
        char resolved_path[MAX_PATH];
        if (_fullpath(resolved_path, full.c_str(), MAX_PATH))
            SDL_SetClipboardText(resolved_path);
#else
        char resolved_path[PATH_MAX];
        if (realpath(full.c_str(), resolved_path))
            SDL_SetClipboardText(resolved_path);
#endif
    }
    if (edge(SDL_CONTROLLER_BUTTON_BACK))
        quit = true;

    memcpy(g_menuPadPrev, cur, sizeof(cur));
}

void handleHLE(R800* cpu);

#ifndef VERIFY_CORE
int main(int argc, char* argv[]) {
#else
int main_emu(int argc, char* argv[]) {
#endif
    const char* targetPath = "."; bool pathIsFile = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) debugMode = true; else if (strcmp(argv[i], "-v") == 0) vramViewerMode = true; else targetPath = argv[i];
    }
    struct stat st; if (stat(targetPath, &st) == 0 && S_ISREG(st.st_mode)) pathIsFile = true;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) return 1;
    register8BitDoMicroMappings();
    tryOpenFirstGameController();
    initVideo(); initSound();
    biosLoaderInit();
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
                if (it != romFiles.end()) {
                    menuSel = std::distance(romFiles.begin(), it);
                    menuOff = (menuSel / MSXPLAY_MENU_PAGE_SIZE) * MSXPLAY_MENU_PAGE_SIZE;
                }
            }
        }
    }
    bool quit = false, fullscreen = false; SDL_Event e; Uint32 lastTime = SDL_GetTicks();
    printf("main: B=cycle BIOS (emb/C-BIOS/C-BIOS JP/VG8020/main+logo/HB-10)  Ctrl+F5  F12=db\n"); fflush(stdout);
    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (!g_gameController && SDL_IsGameController(e.cdevice.which))
                    g_gameController = SDL_GameControllerOpen(e.cdevice.which);
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                SDL_JoystickID jid = e.cdevice.which;
                if (g_gameController) {
                    SDL_Joystick* j = SDL_GameControllerGetJoystick(g_gameController);
                    if (j && SDL_JoystickInstanceID(j) == jid) {
                        closeGameController();
                        tryOpenFirstGameController();
                    }
                }
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    fullscreen = !fullscreen; SDL_SetWindowFullscreen(getMainWindow(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                if (appState == STATE_MENU) {
                    int pageSize = MSXPLAY_MENU_PAGE_SIZE;
                    if (e.key.keysym.sym == SDLK_UP) menuSel--; else if (e.key.keysym.sym == SDLK_DOWN) menuSel++;
                    else if (e.key.keysym.sym == SDLK_PAGEUP || e.key.keysym.sym == SDLK_LEFT) menuSel -= pageSize;
                    else if (e.key.keysym.sym == SDLK_PAGEDOWN || e.key.keysym.sym == SDLK_RIGHT) menuSel += pageSize;
                    else if (e.key.keysym.sym == SDLK_RETURN) {
                        std::string full = baseDir + "/" + romFiles[menuSel];
                        if (loadRom(full.c_str())) { saveLastGame(romFiles[menuSel].c_str()); startEmulator(); appState = STATE_EMU; }
                    } else if (e.key.keysym.sym == SDLK_b && !(e.key.keysym.mod & KMOD_CTRL)) {
                        menuAdvanceBiosMode();
                    } else if (e.key.keysym.sym == SDLK_c && (e.key.keysym.mod & KMOD_CTRL)) {
                        if (!romFiles.empty() && menuSel >= 0 && menuSel < (int)romFiles.size()) {
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
                    if (menuSel < 0) menuSel = 0;
                    if (menuSel >= (int)romFiles.size()) menuSel = (int)romFiles.size() - 1;
                    if (menuSel < menuOff) menuOff = menuSel;
                    if (menuSel >= menuOff + pageSize) menuOff = menuSel - pageSize + 1;
                } else {
                    if (e.key.keysym.sym == SDLK_F7) {
                        if (!romFiles.empty()) {
                            appState = STATE_MENU;
                            menuResetGamepadNavState();
                        } else startEmulator();
                    }
                    if (e.key.keysym.sym == SDLK_F8) scanlinesEnabled = !scanlinesEnabled;
                    if (e.key.keysym.sym == SDLK_PRINTSCREEN) { saveVramSc2("capture.sc2"); saveScreenshot("capture.bmp"); }
                    if (e.key.keysym.sym == SDLK_F5 && (e.key.keysym.mod & KMOD_CTRL)) cycleMapperAndSoftReset();
                    if (e.key.keysym.sym == SDLK_F12 && romData) {
                        if (!g_mapperDbTriedLoad) {
                            g_mapperDb.load(kMapperDbPath);
                            g_mapperDbTriedLoad = true;
                        }
                        std::string sha1 = sha1Hex(romData, romSize);
                        RomDbProfile saveProf;
                        saveProf.mapper = romMapper;
                        saveProf.biosMode = g_biosMode;
                        saveProf.font = 'e';
                        if (g_mapperDb.upsertProfile(kMapperDbPath, sha1, saveProf)) {
                            g_mapperDb.load(kMapperDbPath);
                            printf("mapper_db: saved %s -> %s bios=%u\n", sha1.c_str(), mapperTypeName(romMapper),
                                   (unsigned)g_biosMode);
                        } else {
                            printf("mapper_db: failed to write %s\n", kMapperDbPath);
                        }
                        fflush(stdout);
                    }
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
        } else {
            if (appState == STATE_MENU)
                menuUpdateGamepad(romFiles, menuSel, menuOff, baseDir, quit, appState);
            DrawMenu(romFiles, menuSel, menuOff, (int)menuBiosMode);
            SDL_Delay(16);
        }
    }
    closeGameController();
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
