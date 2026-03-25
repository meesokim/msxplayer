#include "hash_util.h"
#include "mapper_db.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

static bool readFileAll(const std::string& path, std::vector<UInt8>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    if (sz > 0) fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    return true;
}

static void listFilesRec(const std::string& path, std::vector<std::string>& files) {
    DIR* d = opendir(path.c_str());
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string p = path + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) listFilesRec(p, files);
        else files.push_back(p);
    }
    closedir(d);
}

static bool endsWithLower(const std::string& s, const char* ext) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    std::string e = ext;
    return t.size() >= e.size() && t.substr(t.size() - e.size()) == e;
}

static MapperType openMsxTypeToMapper(const std::string& t) {
    if (t == "Konami") return MAPPER_KONAMI;
    if (t == "KonamiSCC") return MAPPER_KONAMI_SCC;
    if (t == "ASCII8") return MAPPER_ASCII8;
    if (t == "ASCII16" || t == "ASCII16SRAM8" || t == "ASCII16SRAM2") return MAPPER_ASCII16;
    return MAPPER_NONE;
}

static std::unordered_map<std::string, MapperType> parseOpenMsxSoftwareDb(const std::string& xmlPath) {
    std::unordered_map<std::string, MapperType> out;
    FILE* f = fopen(xmlPath.c_str(), "r");
    if (!f) return out;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        std::string s = line;
        size_t m = s.find("<megarom><type>");
        if (m == std::string::npos) continue;
        size_t t0 = m + strlen("<megarom><type>");
        size_t t1 = s.find("</type>", t0);
        size_t h0 = s.find("<hash>", t1);
        size_t h1 = s.find("</hash>", h0);
        if (t1 == std::string::npos || h0 == std::string::npos || h1 == std::string::npos) continue;
        std::string t = s.substr(t0, t1 - t0);
        std::string h = s.substr(h0 + 6, h1 - (h0 + 6));
        if (h.size() == 40) out[h] = openMsxTypeToMapper(t);
    }
    fclose(f);
    return out;
}

static int buildMapperDb(const std::string& romRoot, const std::string& openMsxXml, const std::string& outDb) {
    auto sw = parseOpenMsxSoftwareDb(openMsxXml);
    std::vector<std::string> files;
    listFilesRec(romRoot, files);
    FILE* out = fopen(outDb.c_str(), "w");
    if (!out) return 1;
    fprintf(out, "# sha1,mapper\n");
    int total = 0, matched = 0;
    for (const std::string& p : files) {
        if (!(endsWithLower(p, ".rom") || endsWithLower(p, ".mx1") || endsWithLower(p, ".bin"))) continue;
        std::vector<UInt8> rom;
        if (!readFileAll(p, rom)) continue;
        std::string sha1 = sha1Hex(rom);
        MapperType mapper = MAPPER_NONE;
        auto it = sw.find(sha1);
        if (it != sw.end()) {
            mapper = it->second;
            matched++;
        }
        fprintf(out, "%s,%s\n", sha1.c_str(), mapperTypeName(mapper));
        total++;
    }
    fclose(out);
    printf("mapper db written: %s (total=%d, openMSX matched=%d)\n", outDb.c_str(), total, matched);
    return 0;
}

static void renderSc2Reference(const UInt8* vram, UInt16* out) {
    static const UInt16 p[16] = {
        0x0000, 0x0000, 0x3CB9, 0x747A, 0x595C, 0x83B1, 0xB92A, 0x659F,
        0xDB2B, 0xFF9F, 0xCCCB, 0xDE90, 0x3A24, 0xB336, 0xCCCC, 0xFFFF
    };
    const int nt = 0x1800;
    for (int y = 0; y < 192; ++y) {
        int zone = (y / 64) << 11;
        int py = y & 7;
        int row = (y / 8) * 32;
        for (int tx = 0; tx < 32; ++tx) {
            int idx = zone | (vram[(nt + row + tx) & 0x3FFF] << 3) | py;
            UInt8 pat = vram[idx & 0x3FFF];
            UInt8 col = vram[(0x2000 + idx) & 0x3FFF];
            UInt16 fg = p[col >> 4];
            UInt16 bg = p[col & 0x0F];
            int base = y * 256 + tx * 8;
            for (int b = 0; b < 8; ++b) out[base + b] = (pat & (0x80 >> b)) ? fg : bg;
        }
    }
}

static int buildVramRgbDb(const std::string& root, const std::string& outDb) {
    std::vector<std::string> files;
    listFilesRec(root, files);
    FILE* out = fopen(outDb.c_str(), "w");
    if (!out) return 1;
    fprintf(out, "# vram_sha1,rgb_sha1\n");
    int total = 0;
    for (const std::string& p : files) {
        if (!endsWithLower(p, ".sc2")) continue;
        std::vector<UInt8> sc2;
        if (!readFileAll(p, sc2) || sc2.size() < 7 + 0x4000) continue;
        const UInt8* vram = sc2.data() + 7;
        UInt16 rgb[256 * 192];
        renderSc2Reference(vram, rgb);
        std::string vSha = sha1Hex(vram, 0x4000);
        std::string rSha = sha1Hex((const UInt8*)rgb, sizeof(rgb));
        fprintf(out, "%s,%s\n", vSha.c_str(), rSha.c_str());
        total++;
    }
    fclose(out);
    printf("vdp db written: %s (samples=%d)\n", outDb.c_str(), total);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  verify_tools build-mapper-db <rom_root> <openmsx_softwaredb.xml> <out_mapper_db.csv>\n");
        printf("  verify_tools build-vdp-db <sample_root> <out_vdp_db.csv>\n");
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "build-mapper-db" && argc == 5) {
        return buildMapperDb(argv[2], argv[3], argv[4]);
    }
    if (cmd == "build-vdp-db" && argc == 4) {
        return buildVramRgbDb(argv[2], argv[3]);
    }
    printf("Invalid arguments.\n");
    return 1;
}
