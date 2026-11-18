#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fx {

// FXPK：安装载荷容器。打包端与安装端共用这份读写实现，格式只定义一次。
// 布局（小端）：
//   头 32B   magic "FXPK" | version u32 | entryCount u32 | reserved u32 | blobOffset u64 | reserved u64
//   条目 n×  pathLen u32 | path UTF-8（'/' 分隔，相对安装根）| method u32 | compSize u64 | rawSize u64
//            | reserved u32 | sha256[32]
//   之后补零对齐到 blobOffset，再按条目顺序紧密拼接数据块。
inline constexpr uint32_t kFxMagic = 0x4B505846u; // 'F''X''P''K'
inline constexpr uint32_t kFxVersion = 1u;
inline constexpr std::size_t kFxHeaderSize = 32;

enum FxMethod : uint32_t {
    kFxStore = 0,
    kFxDeflate = 1,
};

struct FxItem {
    std::string path;
    uint32_t method = kFxStore;
    std::vector<uint8_t> content; // 写入时的最终字节（deflate 条目已是压缩后的流）
    uint64_t rawSize = 0;         // 解压前大小
    uint8_t digest[32]{};         // 覆盖"落到磁盘上的内容"，不是 content：这样解压错了也能被发现
};

struct FxEntry {
    std::string path;
    uint32_t method = kFxStore;
    uint64_t compSize = 0;
    uint64_t rawSize = 0;
    uint8_t sha256[32]{};
    const uint8_t* data = nullptr; // 指向镜像内的数据块，不复制
};

// 读侧视图：条目里的 data 指向调用方持有的镜像，镜像必须比视图活得久。
struct FxPackageView {
    std::vector<FxEntry> entries;
};

bool fxpWrite(const std::vector<FxItem>& items, std::vector<uint8_t>& out, std::string& error);

bool fxpRead(const uint8_t* image, std::size_t len, FxPackageView& out, std::string& error);

// 把条目里的数据还原成原始字节：store 直接拷，deflate 解压。rawSize 是硬上界。
bool fxMaterialize(const FxEntry& entry, std::vector<uint8_t>& bytes, std::string& error);

} // namespace fx
