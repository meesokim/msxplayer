#ifndef MAPPER_DB_H
#define MAPPER_DB_H

#include <string>
#include <unordered_map>
#include "msxplay.h"

struct RomDbProfile {
    MapperType mapper = MAPPER_NONE;
    /** false=C-BIOS family, true=MSX BASIC ROM (VG8020 / HB-10). */
    bool msxBasic = false;
    /** 'e' intl, 'j' JP (CSV may use k → read as j). */
    char font = 'e';
};

/** Maps DB row to biosLoader mode: 1=C-BIOS 2=VG8020 4=HB-10 5=C-BIOS JP (0/3 not in DB). */
unsigned char romDbProfileBiosMode(const RomDbProfile& p);

/** F12 save: session biosMode → DB basic/none + font. */
void romDbProfileFromSessionBios(RomDbProfile& p, unsigned char biosMode);

class MapperDb {
public:
    bool load(const std::string& path);
    bool find(const std::string& sha1, MapperType& mapper) const;
    bool findProfile(const std::string& sha1, RomDbProfile& out) const;
    /** Legacy: updates mapper only; keeps existing basic/font or defaults. */
    bool upsert(const std::string& path, const std::string& sha1, MapperType mapper);
    bool upsertProfile(const std::string& path, const std::string& sha1, const RomDbProfile& profile);

private:
    std::unordered_map<std::string, RomDbProfile> profiles_;
};

const char* mapperTypeName(MapperType mapper);
MapperType mapperTypeFromName(const std::string& name);

const char* romDbBiosShortLabel(const RomDbProfile& p);
void romDbFormatMenuMeta(const RomDbProfile& p, char* out, size_t outSz);

#endif
