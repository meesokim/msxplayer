#ifndef MAPPER_DB_H
#define MAPPER_DB_H

#include <string>
#include <unordered_map>
#include "msxplay.h"

class MapperDb {
public:
    bool load(const std::string& path);
    bool find(const std::string& sha1, MapperType& mapper) const;
    /** Replace or append one row; updates in-memory map. */
    bool upsert(const std::string& path, const std::string& sha1, MapperType mapper);

private:
    std::unordered_map<std::string, MapperType> map_;
};

const char* mapperTypeName(MapperType mapper);
MapperType mapperTypeFromName(const std::string& name);

#endif
