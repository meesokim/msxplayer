#ifndef MAPPER_DB_H
#define MAPPER_DB_H

#include <string>
#include <unordered_map>
#include "msxplay.h"

struct RomDbProfile {
    MapperType mapper = MAPPER_NONE;
    /** 0=emb 1=C-BIOS intl 2=VG8020 3=main+logo 4=HB-10 5=C-BIOS JP (cbios_main_msx1_jp.rom) */
    unsigned char biosMode = 0;
    char font = 'e'; /* e=international, j=Japanese, k=Korean */
};

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

#endif
