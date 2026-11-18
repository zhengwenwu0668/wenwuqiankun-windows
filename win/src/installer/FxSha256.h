#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fx {

struct Sha256 {
    uint8_t bytes[32]{};
};

// 纯实现，无第三方：安装器要在不依赖 Qt 的前提下逐文件校验载荷完整性。
Sha256 sha256Compute(const uint8_t* data, std::size_t len);

std::string sha256Hex(const Sha256& digest);
bool sha256Equal(const Sha256& a, const uint8_t* b);

} // namespace fx
