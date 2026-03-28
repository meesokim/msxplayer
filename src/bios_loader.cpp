#include "bios_loader.h"
#include "bios_data.h"

#include <stdio.h>
#include <string.h>

static UInt8 s_biosBackup[0x8000];
static UInt8 s_logoBackup[0x4000];
static bool s_inited = false;

void biosLoaderInit(void) {
    if (s_inited) return;
    memcpy(s_biosBackup, bios, sizeof(s_biosBackup));
    memcpy(s_logoBackup, bios_logo, sizeof(s_logoBackup));
    s_inited = true;
}

static bool tryLoadFile(UInt8* dest, size_t destSize, const char* filename, const std::vector<std::string>& dirs) {
    for (size_t i = 0; i < dirs.size(); ++i) {
        std::string path = dirs[i];
        if (path.empty()) continue;
        while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.pop_back();
        std::string full = path + "/" + filename;
        FILE* f = fopen(full.c_str(), "rb");
        if (!f) continue;
        size_t n = fread(dest, 1, destSize, f);
        fclose(f);
        if (n >= 0x1000) {
            if (n < destSize) memset(dest + n, 0xFF, destSize - n);
            printf("biosLoader: loaded %s (%zu bytes)\n", full.c_str(), n);
            fflush(stdout);
            return true;
        }
    }
    return false;
}

static bool tryLoadFirstOf(UInt8* dest, size_t destSize, const char* const* names, const std::vector<std::string>& dirs) {
    for (int i = 0; names[i]; ++i) {
        if (tryLoadFile(dest, destSize, names[i], dirs)) return true;
    }
    return false;
}

static bool applyCbiosMainAndSecondary(const char* mainRom, const char* const* secondaryList,
                                       const std::vector<std::string>& searchDirs, unsigned biosModeForMsg) {
    if (!tryLoadFile(bios, sizeof(bios), mainRom, searchDirs)) {
        printf("biosLoader: FAILED to load main '%s'", mainRom);
        if (biosModeForMsg == 1 || biosModeForMsg == 5)
            printf(" — using embedded BIOS\n");
        fflush(stdout);
        memcpy(bios, s_biosBackup, sizeof(bios));
        memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
        return biosModeForMsg != 1 && biosModeForMsg != 5;
    }

    if (!tryLoadFirstOf(bios_logo, sizeof(bios_logo), secondaryList, searchDirs)) {
        printf("biosLoader: no secondary ROM (tried logo/basic) — using embedded 8000h region\n");
        fflush(stdout);
        memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
    }
    return true;
}

bool biosLoaderApply(unsigned biosMode, const std::vector<std::string>& searchDirs) {
    if (!s_inited) biosLoaderInit();

    unsigned mode = biosMode;
    if (mode > 5) mode = 0;

    if (mode == 0) {
        memcpy(bios, s_biosBackup, sizeof(bios));
        memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
        return true;
    }

    if (mode == 2) {
        if (!tryLoadFile(bios, sizeof(bios), "vg8020_basic-bios1.rom", searchDirs)) {
            printf("biosLoader: vg8020_basic-bios1.rom missing — embedded\n");
            fflush(stdout);
            memcpy(bios, s_biosBackup, sizeof(bios));
            memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
            return false;
        }
        memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
        return true;
    }

    if (mode == 4) {
        if (!tryLoadFile(bios, sizeof(bios), "hb-10_basic-bios1.rom", searchDirs)) {
            printf("biosLoader: hb-10_basic-bios1.rom missing — embedded\n");
            fflush(stdout);
            memcpy(bios, s_biosBackup, sizeof(bios));
            memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
            return false;
        }
        memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
        return true;
    }

    static const char* const secondaryJp[] = {
        "cbios_logo_msx1.rom",
        "cbios_basic.rom",
        nullptr
    };
    static const char* const secondaryEu[] = {
        "cbios_basic.rom",
        "cbios_logo_msx1.rom",
        nullptr
    };

    if (mode == 3) {
        if (!tryLoadFile(bios, sizeof(bios), "cbios_main_msx1.rom", searchDirs)) {
            printf("biosLoader: FAILED to load main 'cbios_main_msx1.rom' (main+logo mode) — embedded\n");
            fflush(stdout);
            memcpy(bios, s_biosBackup, sizeof(bios));
            memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
            return false;
        }
        static const char* const logoOnly[] = { "cbios_logo_msx1.rom", nullptr };
        if (!tryLoadFirstOf(bios_logo, sizeof(bios_logo), logoOnly, searchDirs)) {
            printf("biosLoader: cbios_logo_msx1.rom missing — embedded 8000h\n");
            fflush(stdout);
            memcpy(bios_logo, s_logoBackup, sizeof(bios_logo));
        }
        return true;
    }

    if (mode == 5)
        return applyCbiosMainAndSecondary("cbios_main_msx1_jp.rom", secondaryJp, searchDirs, 5);

    /* mode == 1 */
    return applyCbiosMainAndSecondary("cbios_main_msx1.rom", secondaryEu, searchDirs, 1);
}
