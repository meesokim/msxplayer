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

static std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c + 32);
    }
    return s;
}

static bool parseBoolField(const std::string& s) {
    std::string t = toLower(trim(s));
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

static char parseFontField(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return 'e';
    char c = (char)t[0];
    if (c == 'J' || c == 'j') return 'j';
    if (c == 'K' || c == 'k') return 'k';
    return 'e';
}

/** CSV column "basic": legacy 0/1 or digit 0-3 (bios bundle). */
static unsigned char parseBiosModeField(const std::string& s) {
    std::string t = trim(s);
    if (t.size() == 1 && t[0] >= '0' && t[0] <= '3') return (unsigned char)(t[0] - '0');
    return parseBoolField(s) ? (unsigned char)1 : (unsigned char)0;
}

const char* mapperTypeName(MapperType mapper) {
    switch (mapper) {
        case MAPPER_NONE: return "NONE";
        case MAPPER_KONAMI: return "KONAMI";
        case MAPPER_KONAMI_SCC: return "KONAMI_SCC";
        case MAPPER_ASCII8: return "ASCII8";
        case MAPPER_ASCII8_SRAM2: return "ASCII8SRAM2";
        case MAPPER_ASCII16: return "ASCII16";
        case MAPPER_MIRRORED: return "MIRRORED";
        case MAPPER_PAGE2: return "PAGE2";
        default: return "NONE";
    }
}

MapperType mapperTypeFromName(const std::string& nameIn) {
    std::string name = trim(nameIn);
    if (name == "KONAMI") return MAPPER_KONAMI;
    if (name == "KONAMI_SCC") return MAPPER_KONAMI_SCC;
    if (name == "ASCII8") return MAPPER_ASCII8;
    if (name == "ASCII8SRAM2") return MAPPER_ASCII8_SRAM2;
    if (name == "ASCII16") return MAPPER_ASCII16;
    if (name == "MIRRORED") return MAPPER_MIRRORED;
    if (name == "PAGE2") return MAPPER_PAGE2;
    return MAPPER_NONE;
}

static void splitCsv(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            out.push_back(trim(line.substr(start, i - start)));
            start = i + 1;
        }
    }
}

bool MapperDb::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    profiles_.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || strlen(line) < 10) continue;
        std::vector<std::string> p;
        splitCsv(line, p);
        if (p.size() < 2) continue;
        if (p[0].size() != 40) continue;
        RomDbProfile prof;
        prof.mapper = mapperTypeFromName(p[1]);
        prof.biosMode = p.size() >= 3 ? parseBiosModeField(p[2]) : (unsigned char)0;
        prof.font = p.size() >= 4 ? parseFontField(p[3]) : 'e';
        profiles_[p[0]] = prof;
    }
    fclose(f);
    return true;
}

bool MapperDb::find(const std::string& sha1, MapperType& mapper) const {
    auto it = profiles_.find(sha1);
    if (it == profiles_.end()) return false;
    mapper = it->second.mapper;
    return true;
}

bool MapperDb::findProfile(const std::string& sha1, RomDbProfile& out) const {
    auto it = profiles_.find(sha1);
    if (it == profiles_.end()) return false;
    out = it->second;
    return true;
}

static std::string profileLine(const std::string& sha1, const RomDbProfile& p) {
    char bm = (char)('0' + (p.biosMode > 3 ? 0 : p.biosMode));
    return sha1 + "," + std::string(mapperTypeName(p.mapper)) + "," + std::string(1, bm) + "," + std::string(1, p.font) + "\n";
}

bool MapperDb::upsertProfile(const std::string& path, const std::string& sha1, const RomDbProfile& profile) {
    std::vector<std::string> lines;
    bool replaced = false;
    bool hadHeader = false;
    FILE* fin = fopen(path.c_str(), "r");
    if (fin) {
        char buf[768];
        while (fgets(buf, sizeof(buf), fin)) {
            std::string raw = buf;
            if (raw.empty()) continue;
            if (raw[0] == '#') {
                if (raw.find("sha1") != std::string::npos) hadHeader = true;
                lines.push_back(raw);
                continue;
            }
            std::vector<std::string> p;
            splitCsv(trim(std::string(buf)), p);
            if (!p.empty() && p[0].size() == 40 && p[0] == sha1) {
                if (!replaced) {
                    lines.push_back(profileLine(sha1, profile));
                    replaced = true;
                }
                continue;
            }
            lines.push_back(raw);
        }
        fclose(fin);
    }
    if (!replaced) {
        if (!hadHeader && lines.empty())
            lines.push_back("# sha1,mapper,bios,font  bios: 0=emb 1=C-BIOS+basic 2=VG8020 3=main+logo  font: e/j/k\n");
        lines.push_back(profileLine(sha1, profile));
    }
    FILE* fout = fopen(path.c_str(), "w");
    if (!fout) return false;
    for (const std::string& s : lines) fputs(s.c_str(), fout);
    fclose(fout);
    profiles_[sha1] = profile;
    return true;
}

bool MapperDb::upsert(const std::string& path, const std::string& sha1, MapperType mapper) {
    RomDbProfile p;
    if (!findProfile(sha1, p)) {
        p.biosMode = 0;
        p.font = 'e';
    }
    p.mapper = mapper;
    return upsertProfile(path, sha1, p);
}
