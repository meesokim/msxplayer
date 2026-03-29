#include "msx_dir_index.h"
#include "rom_index_detect.h"
#include "game_issue_tags.h"
#include "hash_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "unzip.h"

const char* kMsxDbFilename() {
    return "msx.db";
}

static std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

static std::string toLowerS(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c + 32);
    }
    return s;
}

static std::string normalizeSha1Key(const std::string& s) {
    std::string o = trim(s);
    for (size_t i = 0; i < o.size(); i++) {
        char c = o[i];
        if (c >= 'A' && c <= 'Z') o[i] = (char)(c + 32);
    }
    return o;
}

static bool romLikeExt(const std::string& low) {
    return low.find(".rom") != std::string::npos || low.find(".zip") != std::string::npos ||
           low.find(".mx1") != std::string::npos || low.find(".mx2") != std::string::npos ||
           low.find(".bin") != std::string::npos;
}

static std::vector<std::string> scanRomFilenames(const std::string& dir) {
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        std::string name = ent->d_name;
        std::string low = toLowerS(name);
        if (romLikeExt(low)) files.push_back(name);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

static bool zipInnerExtOk(const std::string& low) {
    return low.find(".rom") != std::string::npos || low.find(".mx1") != std::string::npos ||
           low.find(".mx2") != std::string::npos || low.find(".bin") != std::string::npos;
}

static bool readZipLargestRom(const char* path, std::vector<UInt8>& romOut, std::string& innerNameOut) {
    unzFile uf = unzOpen(path);
    if (!uf) return false;
    struct Cand {
        std::string name;
        uLong usize;
    };
    std::vector<Cand> cands;
    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        return false;
    }
    do {
        unz_file_info info;
        char name[512];
        if (unzGetCurrentFileInfo(uf, &info, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK) break;
        std::string low = toLowerS(name);
        if (zipInnerExtOk(low) && info.uncompressed_size > 0)
            cands.push_back(Cand{std::string(name), info.uncompressed_size});
    } while (unzGoToNextFile(uf) == UNZ_OK);
    if (cands.empty()) {
        unzClose(uf);
        return false;
    }
    auto bestIt = std::max_element(cands.begin(), cands.end(),
        [](const Cand& a, const Cand& b) { return a.usize < b.usize; });
    if (unzLocateFile(uf, bestIt->name.c_str(), 2) != UNZ_OK) {
        unzClose(uf);
        return false;
    }
    uLong sz = bestIt->usize;
    romOut.resize((size_t)sz);
    if (unzOpenCurrentFile(uf) != UNZ_OK) {
        unzClose(uf);
        return false;
    }
    int zread = unzReadCurrentFile(uf, romOut.data(), (int)sz);
    unzCloseCurrentFile(uf);
    unzClose(uf);
    if (zread != (int)sz) {
        romOut.clear();
        return false;
    }
    innerNameOut = bestIt->name;
    return true;
}

static bool readPlainFile(const char* path, std::vector<UInt8>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)n);
    size_t nr = fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    if (nr != (size_t)n) {
        out.clear();
        return false;
    }
    return true;
}

static void profFromBasicFontCols(const std::string& basicCol, const std::string& fontCol, RomDbProfile& prof) {
    std::string b = toLowerS(trim(basicCol));
    prof.msxBasic = (b == "basic");
    if (!prof.msxBasic) prof.msxBasic = false;
    std::string f = trim(fontCol);
    char c = f.empty() ? 'e' : (char)f[0];
    prof.font = (c == 'j' || c == 'J' || c == 'k' || c == 'K') ? 'j' : 'e';
}

static bool analyzeFile(const std::string& fullPath, const std::string& displayName, MapperDb& mapperDb,
    const GameIssueTags& issueTags, MsxDirGameEntry& ent) {
    ent.filename = displayName;
    ent.loadOk = true;
    ent.errMsg.clear();
    std::vector<UInt8> rom;
    std::string low = toLowerS(displayName);

    if (low.find(".zip") != std::string::npos) {
        std::string inner;
        if (!readZipLargestRom(fullPath.c_str(), rom, inner)) {
            ent.loadOk = false;
            ent.errMsg = "zip";
            ent.sha1.clear();
            ent.mapper = MAPPER_NONE;
            ent.prof = RomDbProfile();
            ent.issue = false;
            return true;
        }
    } else {
        if (!readPlainFile(fullPath.c_str(), rom)) {
            ent.loadOk = false;
            ent.errMsg = "read";
            ent.sha1.clear();
            ent.mapper = MAPPER_NONE;
            ent.prof = RomDbProfile();
            ent.issue = false;
            return true;
        }
    }

    ent.sha1 = sha1Hex(rom.data(), rom.size());
    bool haveProf = false;
    romIndexDetectMapper(rom.data(), (int)rom.size(), mapperDb, ent.mapper, ent.prof, haveProf);
    if (!haveProf)
        ent.prof = RomDbProfile();
    ent.issue = !ent.sha1.empty() && issueTags.contains(ent.sha1);
    return true;
}

static std::string csvEscapeFilename(const std::string& name) {
    if (name.find(',') == std::string::npos && name.find('"') == std::string::npos) return name;
    std::string o = "\"";
    for (char c : name) {
        if (c == '"')
            o += "\"\"";
        else
            o += c;
    }
    o += '"';
    return o;
}

static void splitCsvLoose(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    size_t i = 0;
    if (i < line.size() && line[i] == '"') {
        ++i;
        std::string f;
        while (i < line.size()) {
            if (line[i] == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    f += '"';
                    i += 2;
                    continue;
                }
                ++i;
                break;
            }
            f += line[i++];
        }
        out.push_back(f);
        if (i < line.size() && line[i] == ',') ++i;
    } else {
        size_t c = line.find(',');
        if (c == std::string::npos) {
            out.push_back(line);
            return;
        }
        out.push_back(line.substr(0, c));
        i = c + 1;
    }
    while (i < line.size()) {
        size_t n = line.find(',', i);
        if (n == std::string::npos) {
            out.push_back(trim(line.substr(i)));
            break;
        }
        out.push_back(trim(line.substr(i, n - i)));
        i = n + 1;
    }
}

static bool parseMsxDbLine(const std::vector<std::string>& p, MsxDirGameEntry& ent) {
    if (p.size() < 7) return false;
    ent.filename = p[0];
    ent.sha1 = normalizeSha1Key(p[1]);
    if (ent.sha1.size() != 40) return false;
    ent.mapper = mapperTypeFromName(p[2]);
    profFromBasicFontCols(p[3], p[4], ent.prof);
    ent.issue = (trim(p[5]) == "1" || toLowerS(trim(p[5])) == "true" || toLowerS(trim(p[5])) == "yes");
    ent.loadOk = !(trim(p[6]) == "0" || toLowerS(trim(p[6])) == "false");
    ent.errMsg.clear();
    if (!ent.loadOk && ent.errMsg.empty()) ent.errMsg = "db";
    return true;
}

static bool loadMsxDbFile(const std::string& path, std::vector<MsxDirGameEntry>& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char buf[1024];
    out.clear();
    while (fgets(buf, sizeof(buf), f)) {
        std::string line = trim(std::string(buf));
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        splitCsvLoose(line, cells);
        MsxDirGameEntry ent;
        if (!parseMsxDbLine(cells, ent)) continue;
        out.push_back(ent);
    }
    fclose(f);
    return !out.empty();
}

bool msxDirWriteIndex(const std::string& dir, const std::vector<MsxDirGameEntry>& entries) {
    std::string path = dir + "/" + kMsxDbFilename();
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f,
        "# file,sha1,mapper,basic_or_none,font,issue,ok  issue=known issue tag; ok=1 if ROM payload read OK\n");
    for (const MsxDirGameEntry& e : entries) {
        char fc = (e.prof.font == 'j' || e.prof.font == 'k') ? 'j' : 'e';
        fprintf(f, "%s,%s,%s,%s,%c,%d,%d\n", csvEscapeFilename(e.filename).c_str(), e.sha1.c_str(),
            mapperTypeName(e.mapper), e.prof.msxBasic ? "basic" : "none", fc, e.issue ? 1 : 0, e.loadOk ? 1 : 0);
    }
    fclose(f);
    return true;
}

bool msxDirLoadOrBuildIndex(const std::string& dir, MapperDb& mapperDb, const GameIssueTags& issueTags,
    std::vector<MsxDirGameEntry>& out) {
    std::string dbPath = dir + "/" + kMsxDbFilename();
    struct stat st;
    if (stat(dbPath.c_str(), &st) == 0 && S_ISREG(st.st_mode) && loadMsxDbFile(dbPath, out) && !out.empty()) {
        std::sort(out.begin(), out.end(),
            [](const MsxDirGameEntry& a, const MsxDirGameEntry& b) { return a.filename < b.filename; });
        for (MsxDirGameEntry& e : out) {
            if (!e.sha1.empty()) e.issue = issueTags.contains(e.sha1);
        }
        return true;
    }
    out.clear();

    std::vector<std::string> names = scanRomFilenames(dir);
    out.clear();
    for (const std::string& name : names) {
        std::string full = dir + "/" + name;
        MsxDirGameEntry ent;
        analyzeFile(full, name, mapperDb, issueTags, ent);
        out.push_back(ent);
    }
    msxDirWriteIndex(dir, out);
    return true;
}

bool msxDirSyncAfterMainDbSave(const std::string& dir, const std::string& sha1Hex40, MapperType sessionMapper,
    const RomDbProfile& sessionProf, MapperDb& mapperDb, const GameIssueTags& issueTags,
    std::vector<MsxDirGameEntry>* menuEntriesInOut) {
    std::string dbPath = dir + "/" + kMsxDbFilename();
    struct stat st;
    if (stat(dbPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;

    std::vector<MsxDirGameEntry> entries;
    if (!loadMsxDbFile(dbPath, entries) || entries.empty()) return false;

    std::string key = normalizeSha1Key(sha1Hex40);
    if (key.size() != 40) return false;

    bool any = false;
    for (MsxDirGameEntry& e : entries) {
        if (e.sha1 == key) {
            e.mapper = sessionMapper;
            e.prof.msxBasic = sessionProf.msxBasic;
            e.prof.font = sessionProf.font;
            e.issue = issueTags.contains(key);
            any = true;
        }
    }
    if (!any) return false;

    if (!msxDirWriteIndex(dir, entries)) return false;

    if (menuEntriesInOut) {
        for (size_t i = 0; i < menuEntriesInOut->size(); ++i) {
            MsxDirGameEntry& me = (*menuEntriesInOut)[i];
            if (me.sha1 == key) {
                me.mapper = sessionMapper;
                me.prof.msxBasic = sessionProf.msxBasic;
                me.prof.font = sessionProf.font;
                me.issue = issueTags.contains(key);
            }
        }
    }
    (void)mapperDb;
    return true;
}
