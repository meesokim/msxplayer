#ifndef MAPPER_DB_H
#define MAPPER_DB_H

#include <string>
#include <unordered_map>
#include "msxplay.h"

struct RomDbProfile {
    MapperType mapper = MAPPER_NONE;
    /** 0=embedded 1=C-BIOS main+cbios_basic/logo 2=vg8020_basic-bios1.rom 3=cbios_main+cbios_logo only */
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
