#include "rom_index_detect.h"
#include "hash_util.h"
#include <string>

static void megaHeuristic(const UInt8* romData, int romSize, MapperType* out) {
    int kWrite = 0;
    int ascii8Write = 0;
    int ascii16Write = 0;
    for (int i = 0; i + 2 < romSize; ++i) {
        if (romData[i] == 0x32) {
            UInt16 a = (UInt16)romData[i + 1] | ((UInt16)romData[i + 2] << 8);
            if (a == 0x6000 || a == 0x8000 || a == 0xA000) kWrite++;
            if (a >= 0x6000 && a < 0x8000) ascii8Write++;
            if (a == 0x6000 || a == 0x7000) ascii16Write++;
        }
    }
    if (ascii8Write > kWrite + 2 && ascii8Write >= ascii16Write)
        *out = MAPPER_ASCII8;
    else if (ascii16Write > kWrite + 2)
        *out = MAPPER_ASCII16;
    else
        *out = MAPPER_KONAMI;
}

void romIndexDetectMapper(const UInt8* romData, int romSize, MapperDb& db, MapperType& outMapper, RomDbProfile& outProf,
    bool& outHaveProf) {
    outMapper = MAPPER_NONE;
    outProf = RomDbProfile();
    outHaveProf = false;
    if (!romData || romSize < 1)
        return;

    const std::string sha1 = sha1Hex(romData, (size_t)romSize);
    outHaveProf = db.findProfile(sha1, outProf);

    if (romSize < 0x10000) {
        if (outHaveProf && outProf.mapper != MAPPER_NONE)
            outMapper = outProf.mapper;
        else if (romSize <= 0x4000 && romData[0] == 'A' && romData[1] == 'B') {
            UInt16 initAddr = (UInt16)romData[2] | ((UInt16)romData[3] << 8);
            UInt16 textAddr = (UInt16)romData[8] | ((UInt16)romData[9] << 8);
            if ((textAddr & 0xC000) == 0x8000) {
                if (initAddr == 0 ||
                    (((initAddr & 0xC000) == 0x8000) &&
                     romSize > 0 && romData[initAddr & (romSize - 1)] == 0xC9))
                    outMapper = MAPPER_PAGE2;
            }
        }
        if (outMapper == MAPPER_NONE && !(outHaveProf && outProf.mapper == MAPPER_NONE))
            outMapper = MAPPER_MIRRORED;
        return;
    }

    if (outHaveProf && outProf.mapper != MAPPER_NONE)
        outMapper = outProf.mapper;
    else
        megaHeuristic(romData, romSize, &outMapper);
}
