#include "hash_util.h"

#include <sstream>
#include <iomanip>

static UInt32 rol(UInt32 value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::string sha1Hex(const UInt8* data, size_t size) {
    UInt64 bitLen = (UInt64)size * 8;
    std::vector<UInt8> msg(data, data + size);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back((UInt8)((bitLen >> (i * 8)) & 0xFF));
    }

    UInt32 h0 = 0x67452301;
    UInt32 h1 = 0xEFCDAB89;
    UInt32 h2 = 0x98BADCFE;
    UInt32 h3 = 0x10325476;
    UInt32 h4 = 0xC3D2E1F0;

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        UInt32 w[80];
        for (int i = 0; i < 16; ++i) {
            size_t base = chunk + i * 4;
            w[i] = ((UInt32)msg[base] << 24) |
                   ((UInt32)msg[base + 1] << 16) |
                   ((UInt32)msg[base + 2] << 8) |
                   (UInt32)msg[base + 3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        UInt32 a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            UInt32 f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            UInt32 temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << h0
        << std::setw(8) << h1
        << std::setw(8) << h2
        << std::setw(8) << h3
        << std::setw(8) << h4;
    return oss.str();
}

std::string sha1Hex(const std::vector<UInt8>& data) {
    return sha1Hex(data.data(), data.size());
}
