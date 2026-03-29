#include "mapper_db.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <cctype>

static std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

static std::string normalizeSha1Key(const std::string& s) {
    std::string o = trim(s);
    for (size_t i = 0; i < o.size(); i++) {
        char c = o[i];
        if (c >= 'A' && c <= 'Z') o[i] = (char)(c + 32);
    }
    return o;
}

static std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c + 32);
    }
    return s;
}

static char parseFontFieldLegacy(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return 'e';
    char c = (char)t[0];
    if (c == 'J' || c == 'j') return 'j';
    if (c == 'K' || c == 'k') return 'k';
    return 'e';
}

static char ejFromFontChar(char c) {
    if (c == 'j' || c == 'J' || c == 'k' || c == 'K') return 'j';
    return 'e';
}

static char parseEjField(const std::string& s) {
    return ejFromFontChar(parseFontFieldLegacy(s));
}

unsigned char romDbProfileBiosMode(const RomDbProfile& p) {
    bool jp = (p.font == 'j' || p.font == 'J' || p.font == 'k' || p.font == 'K');
    if (p.msxBasic)
        return jp ? (unsigned char)4 : (unsigned char)2;
    return jp ? (unsigned char)5 : (unsigned char)1;
}

void romDbProfileFromSessionBios(RomDbProfile& p, unsigned char biosMode) {
    p.msxBasic = false;
    p.font = 'e';
    switch (biosMode) {
        case 2:
            p.msxBasic = true;
            p.font = 'e';
            break;
        case 4:
            p.msxBasic = true;
            p.font = 'j';
            break;
        case 5:
            p.msxBasic = false;
            p.font = 'j';
            break;
        case 0:
        case 1:
        case 3:
        default:
            p.msxBasic = false;
            p.font = 'e';
            break;
    }
}

static void applyLegacyDigitBios(RomDbProfile& prof, unsigned char bm, char fontRaw) {
    char fj = ejFromFontChar(fontRaw);
    prof.font = fj;
    switch (bm) {
        case 0:
            prof.msxBasic = false;
            prof.font = 'e';
            break;
        case 1:
            prof.msxBasic = false;
            prof.font = fj;
            break;
        case 2:
            prof.msxBasic = true;
            prof.font = 'e';
            break;
        case 3:
            prof.msxBasic = false;
            prof.font = 'e';
            break;
        case 4:
            prof.msxBasic = true;
            prof.font = 'j';
            break;
        case 5:
            prof.msxBasic = false;
            prof.font = 'j';
            break;
        default:
            prof.msxBasic = false;
            prof.font = 'e';
            break;
    }
}

static bool parseBasicNoneToken(const std::string& s, bool* outBasic) {
    std::string t = toLower(trim(s));
    if (t == "basic") {
        *outBasic = true;
        return true;
    }
    if (t == "none") {
        *outBasic = false;
        return true;
    }
    return false;
}

const char* romDbBiosShortLabel(const RomDbProfile& p) {
    switch (romDbProfileBiosMode(p)) {
        case 1: return "C-BIOS";
        case 2: return "VG8020";
        case 4: return "HB-10";
        case 5: return "C-BIOS JP";
        default: return "C-BIOS";
    }
}

void romDbFormatMenuMeta(const RomDbProfile& p, char* out, size_t outSz) {
    if (!out || outSz < 4) return;
    snprintf(out, outSz, "%s | %s", mapperTypeName(p.mapper), romDbBiosShortLabel(p));
}

const char* mapperTypeName(MapperType mapper) {
    switch (mapper) {
        case MAPPER_NONE: return "NONE";
        case MAPPER_KONAMI: return "KONAMI";
        case MAPPER_KONAMI_SCC: return "KONAMI_SCC";
        case MAPPER_ASCII8: return "ASCII8";
        case MAPPER_ASCII8_SRAM2: return "ASCII8SRAM2";
        case MAPPER_ASCII16: return "ASCII16";
        case MAPPER_MSXWRITE: return "MSXWrite";
        case MAPPER_ASCII16_SRAM2: return "ASCII16SRAM2";
        case MAPPER_MIRRORED: return "MIRRORED";
        case MAPPER_PAGE2: return "PAGE2";
        case MAPPER_RTYPE: return "RTYPE";
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
    if (name == "MSXWrite") return MAPPER_MSXWRITE;
    if (name == "ASCII16SRAM2") return MAPPER_ASCII16_SRAM2;
    if (name == "MIRRORED") return MAPPER_MIRRORED;
    if (name == "PAGE2") return MAPPER_PAGE2;
    if (name == "RTYPE") return MAPPER_RTYPE;
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
        std::string shaKey = normalizeSha1Key(p[0]);
        if (shaKey.size() != 40) continue;
        RomDbProfile prof;
        prof.mapper = mapperTypeFromName(p[1]);
        bool basicTok = false;
        if (p.size() >= 4 && parseBasicNoneToken(p[2], &basicTok)) {
            prof.msxBasic = basicTok;
            prof.font = parseEjField(p[3]);
        } else if (p.size() >= 3) {
            std::string t2 = trim(p[2]);
            if (t2.size() == 1 && t2[0] >= '0' && t2[0] <= '5') {
                unsigned char bm = (unsigned char)(t2[0] - '0');
                char fr = p.size() >= 4 ? (char)parseFontFieldLegacy(p[3]) : 'e';
                applyLegacyDigitBios(prof, bm, fr);
            } else {
                prof.msxBasic = false;
                prof.font = 'e';
            }
        } else {
            prof.msxBasic = false;
            prof.font = 'e';
        }
        profiles_[shaKey] = prof;
    }
    fclose(f);
    return true;
}

bool MapperDb::find(const std::string& sha1, MapperType& mapper) const {
    std::string key = normalizeSha1Key(sha1);
    if (key.size() != 40) return false;
    auto it = profiles_.find(key);
    if (it == profiles_.end()) return false;
    mapper = it->second.mapper;
    return true;
}

bool MapperDb::findProfile(const std::string& sha1, RomDbProfile& out) const {
    std::string key = normalizeSha1Key(sha1);
    if (key.size() != 40) return false;
    auto it = profiles_.find(key);
    if (it == profiles_.end()) return false;
    out = it->second;
    return true;
}

static std::string profileLine(const std::string& sha1, const RomDbProfile& p) {
    std::string b = p.msxBasic ? "basic" : "none";
    char f = (p.font == 'j' || p.font == 'J' || p.font == 'k' || p.font == 'K') ? 'j' : 'e';
    return sha1 + "," + std::string(mapperTypeName(p.mapper)) + "," + b + "," + std::string(1, f) + "\n";
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
            if (!p.empty() && normalizeSha1Key(p[0]) == normalizeSha1Key(sha1)) {
                if (!replaced) {
                    lines.push_back(profileLine(normalizeSha1Key(sha1), profile));
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
            lines.push_back(
                "# sha1,mapper,basic_or_none,font  basic+e=VG8020 basic+j=HB-10 none+e=C-BIOS none+j=C-BIOS JP\n");
        lines.push_back(profileLine(normalizeSha1Key(sha1), profile));
    }
    FILE* fout = fopen(path.c_str(), "w");
    if (!fout) return false;
    for (const std::string& s : lines) fputs(s.c_str(), fout);
    fclose(fout);
    profiles_[normalizeSha1Key(sha1)] = profile;
    return true;
}

bool MapperDb::upsert(const std::string& path, const std::string& sha1, MapperType mapper) {
    RomDbProfile p;
    if (!findProfile(sha1, p)) {
        p.msxBasic = false;
        p.font = 'e';
    }
    p.mapper = mapper;
    return upsertProfile(path, sha1, p);
}
