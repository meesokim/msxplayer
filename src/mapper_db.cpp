#include "mapper_db.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

const char* mapperTypeName(MapperType mapper) {
    switch (mapper) {
        case MAPPER_NONE: return "NONE";
        case MAPPER_KONAMI: return "KONAMI";
        case MAPPER_KONAMI_SCC: return "KONAMI_SCC";
        case MAPPER_ASCII8: return "ASCII8";
        case MAPPER_ASCII16: return "ASCII16";
        default: return "NONE";
    }
}

MapperType mapperTypeFromName(const std::string& nameIn) {
    std::string name = trim(nameIn);
    if (name == "KONAMI") return MAPPER_KONAMI;
    if (name == "KONAMI_SCC") return MAPPER_KONAMI_SCC;
    if (name == "ASCII8") return MAPPER_ASCII8;
    if (name == "ASCII16") return MAPPER_ASCII16;
    return MAPPER_NONE;
}

bool MapperDb::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    map_.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || strlen(line) < 10) continue;
        char* comma = strchr(line, ',');
        if (!comma) continue;
        *comma = 0;
        std::string sha1 = trim(line);
        std::string mapper = trim(comma + 1);
        if (sha1.size() == 40) {
            map_[sha1] = mapperTypeFromName(mapper);
        }
    }
    fclose(f);
    return true;
}

bool MapperDb::find(const std::string& sha1, MapperType& mapper) const {
    auto it = map_.find(sha1);
    if (it == map_.end()) return false;
    mapper = it->second;
    return true;
}

bool MapperDb::upsert(const std::string& path, const std::string& sha1, MapperType mapper) {
    std::vector<std::string> lines;
    bool replaced = false;
    bool hadHeader = false;
    FILE* fin = fopen(path.c_str(), "r");
    if (fin) {
        char buf[512];
        while (fgets(buf, sizeof(buf), fin)) {
            std::string raw = buf;
            if (raw.empty()) continue;
            if (raw[0] == '#') {
                if (raw.find("sha1") != std::string::npos) hadHeader = true;
                lines.push_back(raw);
                continue;
            }
            char* comma = strchr(buf, ',');
            if (comma) {
                *comma = '\0';
                std::string hs = trim(buf);
                if (hs.size() == 40 && hs == sha1) {
                    if (!replaced) {
                        lines.push_back(sha1 + "," + std::string(mapperTypeName(mapper)) + "\n");
                        replaced = true;
                    }
                    continue;
                }
            }
            lines.push_back(raw);
        }
        fclose(fin);
    }
    if (!replaced) {
        if (!hadHeader && lines.empty())
            lines.push_back("# sha1,mapper\n");
        lines.push_back(sha1 + "," + std::string(mapperTypeName(mapper)) + "\n");
    }
    FILE* fout = fopen(path.c_str(), "w");
    if (!fout) return false;
    for (const std::string& s : lines) fputs(s.c_str(), fout);
    fclose(fout);
    map_[sha1] = mapper;
    return true;
}
