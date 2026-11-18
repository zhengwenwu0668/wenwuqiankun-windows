#include "FxPackage.h"

#include "FxInflate.h"

#include <cstring>
#include <numeric>

namespace fx {
namespace {

constexpr std::size_t kMaxEntries = 200000;
constexpr std::size_t kMaxPathLen = 1024;
constexpr std::size_t kMaxSpan = 1ull << 31; // 单个条目不超过 2 GiB

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
}

void putU64(std::vector<uint8_t>& out, uint64_t v) {
    putU32(out, uint32_t(v));
    putU32(out, uint32_t(v >> 32));
}

bool getU32(const uint8_t* p, std::size_t len, std::size_t& at, uint32_t& v) {
    if (at + 4 > len) {
        return false;
    }
    v = uint32_t(p[at]) | (uint32_t(p[at + 1]) << 8) | (uint32_t(p[at + 2]) << 16) | (uint32_t(p[at + 3]) << 24);
    at += 4;
    return true;
}

bool getU64(const uint8_t* p, std::size_t len, std::size_t& at, uint64_t& v) {
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!getU32(p, len, at, lo) || !getU32(p, len, at, hi)) {
        return false;
    }
    v = uint64_t(lo) | (uint64_t(hi) << 32);
    return true;
}

// 条目路径直接决定安装器往哪写文件，读写两端各拦一次：不许绝对路径、盘符、反斜杠或跳出目录。
bool isSafeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathLen) {
        return false;
    }
    if (path.front() == '/' || path.back() == '/' || path.find('\\') != std::string::npos
        || path.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

} // namespace

bool fxpWrite(const std::vector<FxItem>& items, std::vector<uint8_t>& out, std::string& error) {
    if (items.empty()) {
        error = "empty package";
        return false;
    }
    if (items.size() > kMaxEntries) {
        error = "too many entries";
        return false;
    }

    std::size_t manifest = 0;
    std::size_t blobs = 0;
    for (const auto& item : items) {
        if (!isSafeRelativePath(item.path)) {
            error = "unsafe entry path: " + item.path;
            return false;
        }
        if (item.method != kFxStore && item.method != kFxDeflate) {
            error = "unknown method for " + item.path;
            return false;
        }
        if (item.method == kFxStore && item.content.size() != size_t(item.rawSize)) {
            error = "store size mismatch for " + item.path;
            return false;
        }
        if (item.content.size() > kMaxSpan || item.rawSize > kMaxSpan) {
            error = "entry too large: " + item.path;
            return false;
        }
        manifest += 4 + item.path.size() + 4 + 8 + 8 + 4 + 32;
        blobs += item.content.size();
    }

    const std::size_t blobOffset = kFxHeaderSize + manifest;
    out.clear();
    out.reserve(blobOffset + blobs);

    putU32(out, kFxMagic);
    putU32(out, kFxVersion);
    putU32(out, uint32_t(items.size()));
    putU32(out, 0);
    putU64(out, blobOffset);
    putU64(out, 0);

    for (const auto& item : items) {
        putU32(out, uint32_t(item.path.size()));
        out.insert(out.end(), item.path.begin(), item.path.end());
        putU32(out, item.method);
        putU64(out, item.content.size());
        putU64(out, item.rawSize);
        putU32(out, 0);
        out.insert(out.end(), item.digest, item.digest + 32);
    }

    for (const auto& item : items) {
        out.insert(out.end(), item.content.begin(), item.content.end());
    }
    return out.size() == blobOffset + blobs;
}

bool fxpRead(const uint8_t* image, std::size_t len, FxPackageView& out, std::string& error) {
    out.entries.clear();
    if (len < kFxHeaderSize) {
        error = "package shorter than header";
        return false;
    }

    std::size_t at = 0;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    uint32_t scratch = 0;
    uint64_t blobOffset = 0;
    uint64_t reservedTail = 0;
    if (!getU32(image, len, at, magic) || !getU32(image, len, at, version) || !getU32(image, len, at, count)
        || !getU32(image, len, at, scratch) || !getU64(image, len, at, blobOffset)
        || !getU64(image, len, at, reservedTail)) {
        error = "truncated header";
        return false;
    }
    if (magic != kFxMagic) {
        error = "bad magic";
        return false;
    }
    if (version != kFxVersion) {
        error = "unsupported package version";
        return false;
    }
    if (count == 0 || count > kMaxEntries || blobOffset > len) {
        error = "implausible package geometry";
        return false;
    }

    std::vector<FxEntry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pathLen = 0;
        if (!getU32(image, len, at, pathLen) || pathLen == 0 || pathLen > kMaxPathLen || at + pathLen > len) {
            error = "bad entry path";
            return false;
        }
        FxEntry entry;
        entry.path.assign(reinterpret_cast<const char*>(image + at), pathLen);
        at += pathLen;
        if (!getU32(image, len, at, entry.method) || !getU64(image, len, at, entry.compSize)
            || !getU64(image, len, at, entry.rawSize) || !getU32(image, len, at, scratch)) {
            error = "truncated entry";
            return false;
        }
        if (at + 32 > len) {
            error = "truncated digest";
            return false;
        }
        std::memcpy(entry.sha256, image + at, 32);
        at += 32;

        if (!isSafeRelativePath(entry.path) || (entry.method != kFxStore && entry.method != kFxDeflate)
            || entry.compSize > kMaxSpan || entry.rawSize > kMaxSpan) {
            error = "rejected entry: " + entry.path;
            return false;
        }
        entries.push_back(std::move(entry));
    }

    if (at > size_t(blobOffset)) {
        error = "manifest overruns blob area";
        return false;
    }

    std::size_t cursor = size_t(blobOffset);
    for (auto& entry : entries) {
        if (cursor > len || entry.compSize > len - cursor) {
            error = "blob runs past the end of the package";
            return false;
        }
        entry.data = image + cursor;
        cursor += entry.compSize;
    }

    out.entries = std::move(entries);
    return true;
}

bool fxMaterialize(const FxEntry& entry, std::vector<uint8_t>& bytes, std::string& error) {
    bytes.clear();
    if (entry.method == kFxStore) {
        if (entry.compSize != entry.rawSize) {
            error = "store size mismatch: " + entry.path;
            return false;
        }
        bytes.assign(entry.data, entry.data + entry.compSize);
        return true;
    }

    bytes.resize(size_t(entry.rawSize));
    std::size_t written = 0;
    if (!inflateRaw(entry.data, size_t(entry.compSize), bytes.data(), bytes.size(), written)
        || written != bytes.size()) {
        error = "inflate failed: " + entry.path;
        bytes.clear();
        return false;
    }
    return true;
}

} // namespace fx
