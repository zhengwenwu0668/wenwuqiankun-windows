#pragma once

#include <cstddef>
#include <cstdint>

namespace fx {

// 输入 RFC1951 raw DEFLATE 流（不含 zlib / gzip 外壳），纯 C++，不引第三方压缩库。
// dstLen 必须等于打包时记录的原始大小：解压过程逐字节受这个上界约束，超限即失败。
bool inflateRaw(const uint8_t* src, std::size_t srcLen, uint8_t* dst, std::size_t dstLen, std::size_t& written);

// 校验 data 是一段合法 zlib（RFC1952）流的开头，并给出其中 raw DEFLATE 数据的偏移。
// 偏移相对 data 本身，不含 qCompress 前置的 4 字节长度。带字典的 zlib 流一律拒收。
bool stripZlibHeader(const uint8_t* data, std::size_t len, std::size_t& deflateOffset);

} // namespace fx
