#ifndef MSX_DIR_INDEX_H
#define MSX_DIR_INDEX_H

#include <string>
#include <vector>
#include "msxplay.h"
#include "mapper_db.h"

class GameIssueTags;

/** One row of directory-local msx.db (CSV). Cart SHA1 = file or largest inner ROM in ZIP. */
struct MsxDirGameEntry {
    std::string filename;
    std::string sha1;
    MapperType mapper = MAPPER_NONE;
    RomDbProfile prof;
    bool issue = false;
    bool loadOk = true;
    std::string errMsg;
};

const char* kMsxDbFilename();

/** Load msx.db from dir, or scan ROM/ZIP files, analyze, write msx.db, fill out. */
bool msxDirLoadOrBuildIndex(const std::string& dir, MapperDb& mapperDb, const GameIssueTags& issueTags,
    std::vector<MsxDirGameEntry>& out);

/** After mapper_db (main) update: refresh rows with same cart SHA1 and rewrite msx.db. */
bool msxDirSyncAfterMainDbSave(const std::string& dir, const std::string& sha1Hex40, MapperType sessionMapper,
    const RomDbProfile& sessionProf, MapperDb& mapperDb, const GameIssueTags& issueTags,
    std::vector<MsxDirGameEntry>* menuEntriesInOut);

/** Rewrite msx.db from current vector (e.g. after E/U issue toggle). */
bool msxDirWriteIndex(const std::string& dir, const std::vector<MsxDirGameEntry>& entries);

#endif
