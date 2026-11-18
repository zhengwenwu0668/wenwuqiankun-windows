#include "FxInflate.h"

#include <cstring>
#include <vector>

namespace fx {
namespace {

class BitReader {
public:
    BitReader(const uint8_t* data, std::size_t len) : data_(data), len_(len) {}

    bool getBit(uint32_t& bit) {
        if (count_ == 0) {
            if (pos_ >= len_) {
                return false;
            }
            byte_ = data_[pos_++];
            count_ = 8;
        }
        bit = byte_ & 1u;
        byte_ >>= 1;
        --count_;
        return true;
    }

    bool getBits(int width, uint32_t& value) {
        value = 0;
        for (int i = 0; i < width; ++i) {
            uint32_t bit = 0;
            if (!getBit(bit)) {
                return false;
            }
            value |= bit << i;
        }
        return true;
    }

    // 非压缩块要求回到字节边界；丢弃当前已在手中但未用完的位即可。
    void discardToByteBoundary() { count_ = 0; }

    bool getBytes(uint8_t* out, std::size_t count) {
        if (count_ != 0 || pos_ + count > len_) {
            return false;
        }
        std::memcpy(out, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    bool getRawByte(uint32_t& value) { return getBits(8, value); }

private:
    const uint8_t* data_ = nullptr;
    std::size_t len_ = 0;
    std::size_t pos_ = 0;
    uint32_t byte_ = 0;
    int count_ = 0;
};

struct Huffman {
    int counts[16]{};
    std::vector<int> symbols;
};

void buildHuffman(Huffman& h, const uint8_t* lengths, int n) {
    for (int i = 0; i < 16; ++i) {
        h.counts[i] = 0;
    }
    for (int i = 0; i < n; ++i) {
        ++h.counts[lengths[i]];
    }
    h.counts[0] = 0;

    int offsets[16]{};
    for (int i = 1; i < 15; ++i) {
        offsets[i + 1] = offsets[i] + h.counts[i];
    }
    h.symbols.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        if (lengths[i] != 0) {
            h.symbols[offsets[lengths[i]]++] = i;
        }
    }
}

int decodeSymbol(BitReader& bits, const Huffman& h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= 15; ++len) {
        uint32_t bit = 0;
        if (!bits.getBit(bit)) {
            return -1;
        }
        code |= int(bit);
        const int count = h.counts[len];
        if (code - count < first) {
            return h.symbols[size_t(index) + size_t(code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,  31,  35,  43,
                                   51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,    33,
                                    49,   65,   97,   129,  193,  257,  385,  513,   769,   1025,
                                    1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13};

constexpr int kClenOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

void fixedLiteral(Huffman& lit, Huffman& dist) {
    uint8_t litLengths[288];
    for (int i = 0; i < 144; ++i) {
        litLengths[i] = 8;
    }
    for (int i = 144; i < 256; ++i) {
        litLengths[i] = 9;
    }
    for (int i = 256; i < 280; ++i) {
        litLengths[i] = 7;
    }
    for (int i = 280; i < 288; ++i) {
        litLengths[i] = 8;
    }
    buildHuffman(lit, litLengths, 288);
    uint8_t distLengths[30];
    for (int i = 0; i < 30; ++i) {
        distLengths[i] = 5;
    }
    buildHuffman(dist, distLengths, 30);
}

bool dynamicHeader(BitReader& bits, Huffman& lit, Huffman& dist) {
    uint32_t hlit = 0;
    uint32_t hdist = 0;
    uint32_t hclen = 0;
    if (!bits.getBits(5, hlit) || !bits.getBits(5, hdist) || !bits.getBits(4, hclen)) {
        return false;
    }
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 288 || hdist > 30) {
        return false;
    }

    uint8_t clenLengths[19]{};
    for (uint32_t i = 0; i < hclen; ++i) {
        uint32_t v = 0;
        if (!bits.getBits(3, v)) {
            return false;
        }
        clenLengths[kClenOrder[i]] = uint8_t(v);
    }
    Huffman clen;
    buildHuffman(clen, clenLengths, 19);

    std::vector<uint8_t> lengths(size_t(hlit) + size_t(hdist), 0);
    std::size_t i = 0;
    const std::size_t total = lengths.size();
    while (i < total) {
        const int sym = decodeSymbol(bits, clen);
        if (sym < 0 || sym > 18) {
            return false;
        }
        if (sym < 16) {
            lengths[i++] = uint8_t(sym);
            continue;
        }
        uint32_t repeat = 0;
        uint8_t value = sym == 16 && i > 0 ? lengths[i - 1] : 0;
        if (sym == 16) {
            if (!bits.getBits(2, repeat) || i == 0) {
                return false;
            }
            repeat += 3;
        } else if (sym == 17) {
            if (!bits.getBits(3, repeat)) {
                return false;
            }
            repeat += 3;
        } else {
            if (!bits.getBits(7, repeat)) {
                return false;
            }
            repeat += 11;
        }
        if (i + repeat > total) {
            return false;
        }
        for (; repeat > 0; --repeat) {
            lengths[i++] = value;
        }
    }

    buildHuffman(lit, lengths.data(), int(hlit));
    buildHuffman(dist, lengths.data() + hlit, int(hdist));
    return true;
}

bool inflateBlock(BitReader& bits, uint8_t* dst, std::size_t dstLen, std::size_t& at, const Huffman& lit,
                  const Huffman& dist) {
    for (;;) {
        const int sym = decodeSymbol(bits, lit);
        if (sym < 0) {
            return false;
        }
        if (sym < 256) {
            if (at >= dstLen) {
                return false;
            }
            dst[at++] = uint8_t(sym);
            continue;
        }
        if (sym == 256) {
            return true;
        }

        const int lenIndex = sym - 257;
        if (lenIndex >= 29) {
            return false;
        }
        uint32_t extra = 0;
        if (!bits.getBits(kLenExtra[lenIndex], extra)) {
            return false;
        }
        std::size_t length = size_t(kLenBase[lenIndex]) + extra;

        const int distSym = decodeSymbol(bits, dist);
        if (distSym < 0 || distSym >= 30) {
            return false;
        }
        if (!bits.getBits(kDistExtra[distSym], extra)) {
            return false;
        }
        const std::size_t distance = size_t(kDistBase[distSym]) + extra;
        if (distance > at || length > dstLen - at) {
            return false;
        }

        // 逐字节拷贝，天然覆盖 distance < length 的游程式重叠。
        const std::size_t from = at - distance;
        for (std::size_t k = 0; k < length; ++k) {
            dst[at + k] = dst[from + k];
        }
        at += length;
    }
}

bool storedBlock(BitReader& bits, uint8_t* dst, std::size_t dstLen, std::size_t& at) {
    bits.discardToByteBoundary();
    uint8_t head[4]{};
    if (!bits.getBytes(head, 4)) {
        return false;
    }
    const uint32_t length = uint32_t(head[0]) | (uint32_t(head[1]) << 8);
    const uint32_t nlength = uint32_t(head[2]) | (uint32_t(head[3]) << 8);
    if ((length ^ 0xFFFFu) != nlength) {
        return false;
    }
    if (at + length > dstLen) {
        return false;
    }
    return bits.getBytes(dst + at, length) ? (at += length, true) : false;
}

} // namespace

bool stripZlibHeader(const uint8_t* data, std::size_t len, std::size_t& deflateOffset) {
    if (len < 2) {
        return false;
    }
    const uint32_t cmf = data[0];
    const uint32_t flg = data[1];
    // CM 必须是 8（deflate），CINFO 给出窗口大小，FCHECK 让 CMF<<8|FLG 被 31 整除。
    if ((cmf & 0x0Fu) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0) {
        return false;
    }
    // FDICT 会让头里多插 2 字节字典 ID；打包端不会用到，见到就直接判非法。
    if ((flg & 0x20u) != 0) {
        return false;
    }
    deflateOffset = 2;
    return true;
}

bool inflateRaw(const uint8_t* src, std::size_t srcLen, uint8_t* dst, std::size_t dstLen, std::size_t& written) {
    written = 0;
    if (dstLen == 0) {
        return true;
    }
    BitReader bits(src, srcLen);
    std::size_t at = 0;

    for (;;) {
        uint32_t finalBit = 0;
        uint32_t type = 0;
        if (!bits.getBit(finalBit) || !bits.getBits(2, type)) {
            return false;
        }

        bool ok = false;
        if (type == 0) {
            ok = storedBlock(bits, dst, dstLen, at);
        } else {
            Huffman lit;
            Huffman dist;
            if (type == 1) {
                fixedLiteral(lit, dist);
            } else if (type == 2) {
                if (!dynamicHeader(bits, lit, dist)) {
                    return false;
                }
            } else {
                return false;
            }
            ok = inflateBlock(bits, dst, dstLen, at, lit, dist);
        }
        if (!ok) {
            return false;
        }
        if (finalBit != 0) {
            break;
        }
    }

    written = at;
    return at == dstLen;
}

} // namespace fx
