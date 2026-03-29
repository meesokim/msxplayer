#ifndef HASH_UTIL_H
#define HASH_UTIL_H

#include <string>
#include <vector>
#include "MsxTypes.h"

std::string sha1Hex(const UInt8* data, size_t size);
std::string sha1Hex(const std::vector<UInt8>& data);
/** Full-file SHA1; empty string on failure. */
std::string sha1HexFile(const char* path);

#endif
