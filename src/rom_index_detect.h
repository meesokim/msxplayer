#ifndef ROM_INDEX_DETECT_H
#define ROM_INDEX_DETECT_H

#include "msxplay.h"
#include "mapper_db.h"

/** Mapper + DB profile for a ROM buffer (no globals; used for msx.db indexing). */
void romIndexDetectMapper(const UInt8* romData, int romSize, MapperDb& db, MapperType& outMapper, RomDbProfile& outProf,
    bool& outHaveProf);

#endif
