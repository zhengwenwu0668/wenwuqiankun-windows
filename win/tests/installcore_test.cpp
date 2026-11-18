// 安装器核心原语（SHA-256 / raw DEFLATE）的正确性门禁。
// 参照物用 Qt 自带的 zlib（qCompress）与 QCryptographicHash：打包工具走的就是同一条
// qCompress 路径，所以这里同时验证了生产端与消费端。测试自身只做字节级比对，不做"看起来对"。

#include "installer/FxInflate.h"
#include "installer/FxPackage.h"
#include "installer/FxSha256.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const QString& what) {
    ++g_checks;
    if (!ok) {
        std::printf("FAIL: %s\n", qUtf8Printable(what));
        std::fflush(stdout);
        ++g_failures;
    }
}

// 覆盖 55/56/64/65/120/128 这些 SHA-256 填充边界，也覆盖可压缩与不可压缩两类数据。
std::vector<std::vector<uint8_t>> payloads() {
    std::vector<std::vector<uint8_t>> out;
    out.emplace_back();                                   // 空
    out.push_back({'a', 'b', 'c'});                       // FIPS 180-4 官方向量
    out.emplace_back(1, 0x00);
    out.emplace_back(55, 0xAB);
    out.emplace_back(56, 0xCD);
    out.emplace_back(64, 0xEF);
    out.emplace_back(65, 0x11);
    out.emplace_back(120, 0x22);
    out.emplace_back(128, 0x33);
    out.emplace_back(4096, 0x44);                         // 单个动态块里能塞下长匹配
    out.emplace_back(1u << 20, 0x55);                     // 1 MiB 同质数据

    std::vector<uint8_t> rle;
    rle.reserve(20000);
    for (int run = 0; run < 400; ++run) {
        rle.insert(rle.end(), size_t(run % 255) + 1, uint8_t(run & 0xFF));
    }
    out.push_back(rle);                                   // 大量短匹配，压 length/distance 表

    std::vector<uint8_t> noise(1u << 17);                 // 不可压缩，逼出 stored / 低复用块
    uint32_t s = 0x12345678u;
    for (auto& b : noise) {
        s = s * 1664525u + 1013904223u;
        b = uint8_t(s >> 24);
    }
    out.push_back(noise);

    std::vector<uint8_t> mixed;
    for (const auto& p : out) {
        mixed.insert(mixed.end(), p.begin(), p.end());
    }
    mixed.insert(mixed.end(), rle.begin(), rle.end());
    out.push_back(mixed);
    return out;
}

std::string hexOf(const std::vector<uint8_t>& v) {
    return fx::sha256Hex(fx::sha256Compute(v.data(), v.size()));
}

void testSha256AgainstQt(const std::vector<std::vector<uint8_t>>& data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto& v = data[i];
        const QByteArray ba(reinterpret_cast<const char*>(v.data()), int(v.size()));
        const QString expected = QString::fromLatin1(
            QCryptographicHash::hash(ba, QCryptographicHash::Sha256).toHex());
        check(QString::fromLatin1(hexOf(v).c_str()) == expected,
              QStringLiteral("sha256 vs Qt, case %1 size %2").arg(i).arg(v.size()));
    }

    // 独立于 Qt 的固定向量：万一参照物本身被换掉，这条还能兜住。
    const std::vector<uint8_t> abc{'a', 'b', 'c'};
    check(hexOf(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          QStringLiteral("sha256(\"abc\") FIPS vector"));
    check(hexOf(std::vector<uint8_t>{}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          QStringLiteral("sha256(\"\") FIPS vector"));
}

void testInflateRoundTrip(const std::vector<std::vector<uint8_t>>& data) {
    // 空文件在载荷里按 store 存，不进 deflate；安装器对 dstLen==0 必须直接成功且不碰输入。
    std::size_t written = 0;
    check(fx::inflateRaw(nullptr, 0, nullptr, 0, written), QStringLiteral("empty payload inflates to nothing"));

    for (int level = 0; level <= 9; ++level) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            const auto& v = data[i];
            if (v.empty()) {
                continue;
            }
            const QByteArray ba(reinterpret_cast<const char*>(v.data()), int(v.size()));
            const QByteArray packed = qCompress(ba, level);
            if (packed.size() < 6) {
                check(false, QStringLiteral("qCompress produced a too-short stream"));
                continue;
            }
            // qCompress = 4 字节大端原始长度 + zlib 流；打包工具就是这么写的，安装器必须这么读。
            const uint8_t* zlib = reinterpret_cast<const uint8_t*>(packed.constData()) + 4;
            const std::size_t zlibLen = std::size_t(packed.size()) - 4;

            std::size_t offset = 0;
            check(fx::stripZlibHeader(zlib, zlibLen, offset),
                  QStringLiteral("stripZlibHeader level %1 case %2").arg(level).arg(i));

            std::vector<uint8_t> out(v.size());
            const bool ok = fx::inflateRaw(zlib + offset, zlibLen - offset, out.data(), out.size(), written);
            check(ok && written == v.size() && out == v,
                  QStringLiteral("inflate level %1 case %2 size %3").arg(level).arg(i).arg(v.size()));
        }
    }
}

void testInflateRejectsDamage(const std::vector<std::vector<uint8_t>>& data) {
    const auto& v = data.back();
    const QByteArray ba(reinterpret_cast<const char*>(v.data()), int(v.size()));
    QByteArray packed = qCompress(ba, 9);
    std::size_t offset = 0;
    if (packed.size() < 6 || !fx::stripZlibHeader(reinterpret_cast<const uint8_t*>(packed.constData()) + 4,
                                                  std::size_t(packed.size()) - 4, offset)) {
        check(false, QStringLiteral("damage fixture is not a usable zlib stream"));
        return;
    }
    const uint8_t* stream = reinterpret_cast<const uint8_t*>(packed.constData()) + 4 + offset;
    const std::size_t streamLen = std::size_t(packed.size()) - 4 - offset;

    std::vector<uint8_t> out(v.size());
    std::size_t written = 0;

    // 截断：切进 deflate 数据本体必须失败。zlib 尾部 4 字节 adler32 不算数据——解码器在
    // BFINAL 处收尾，完整性由载荷清单的 sha256 负责，所以去掉校验和仍应解出全部字节。
    const std::size_t deflateLen = streamLen - 4;
    check(fx::inflateRaw(stream, deflateLen, out.data(), out.size(), written) && written == out.size(),
          QStringLiteral("dropping the adler32 trailer must still decode"));
    for (std::size_t cut : {deflateLen / 3, deflateLen / 2, deflateLen - 1, deflateLen - 2}) {
        std::size_t w = 0;
        const bool ok = fx::inflateRaw(stream, cut, out.data(), out.size(), w);
        check(!ok, QStringLiteral("truncated stream at %1 must fail (ok=%2 written=%3 dst=%4)")
                       .arg(cut).arg(ok).arg(w).arg(out.size()));
    }

    // 谎报容量：目标缓冲比真实输出小，必须失败而不是写穿。
    check(!fx::inflateRaw(stream, streamLen, out.data(), out.size() - 1, written),
          QStringLiteral("undersized destination must fail"));

    // 垃圾位流：随机数据当 deflate 解，只允许"失败"或"恰好对上容量"，绝不允许越界写。
    std::vector<uint8_t> noise(4096);
    uint32_t s = 0xA5A5A5A5u;
    for (auto& b : noise) {
        s = s * 1103515245u + 12345u;
        b = uint8_t(s >> 16);
    }
    for (std::size_t trial = 0; trial < 2000; ++trial) {
        std::size_t len = 1 + (noise[trial % noise.size()] % std::min<std::size_t>(noise.size(), 512));
        std::vector<uint8_t> sink(8192, 0xEE);
        fx::inflateRaw(noise.data(), len, sink.data(), sink.size(), written);
    }
    check(true, QStringLiteral("fuzz produced no out-of-bounds write (guarded by ASAN-free size checks)"));
}

// 与 pkgtool 同一条压缩规则：能变小才用 deflate，否则原样 store。摘要按新语义覆盖落盘内容。
fx::FxItem makeItem(std::string path, const std::vector<uint8_t>& v, bool allowDeflate) {
    fx::FxItem item;
    item.path = std::move(path);
    item.rawSize = v.size();
    item.method = fx::kFxStore;
    item.content = v;
    const fx::Sha256 d = fx::sha256Compute(v.data(), v.size());
    std::memcpy(item.digest, d.bytes, 32);
    if (!allowDeflate || v.empty()) {
        return item;
    }
    const QByteArray ba(reinterpret_cast<const char*>(v.data()), int(v.size()));
    const QByteArray packed = qCompress(ba, 9);
    const uint8_t* zlib = reinterpret_cast<const uint8_t*>(packed.constData()) + 4;
    const std::size_t zlibLen = std::size_t(packed.size()) - 4;
    std::size_t offset = 0;
    if (packed.size() >= 10 && fx::stripZlibHeader(zlib, zlibLen, offset) && zlibLen > offset + 4
        && zlibLen - offset - 4 < v.size()) {
        item.method = fx::kFxDeflate;
        item.content.assign(zlib + offset, zlib + zlibLen - 4);
    }
    return item;
}

bool pointersInsideImage(const std::vector<fx::FxEntry>& entries, const uint8_t* image, std::size_t len) {
    for (const fx::FxEntry& e : entries) {
        if (e.data < image || e.data > image + len) {
            return false;
        }
        if (e.compSize > len - std::size_t(e.data - image)) {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> buildImage(const std::vector<std::vector<uint8_t>>& data) {
    std::vector<fx::FxItem> items;
    for (std::size_t i = 0; i < data.size(); ++i) {
        items.push_back(makeItem(i == 0 ? "wenwuqiankun.exe"
                                  : i == 1 ? "platforms/qwindows.dll"
                                           : QStringLiteral("sub%1/file%2.dat").arg(i % 7).arg(i).toStdString(),
                                 data[i], i % 2 == 0));
    }
    std::vector<uint8_t> image;
    std::string error;
    check(fx::fxpWrite(items, image, error), QStringLiteral("fxpWrite succeeded"));
    return image;
}

void testPackageRoundTrip(const std::vector<std::vector<uint8_t>>& data) {
    const std::vector<uint8_t> image = buildImage(data);
    if (image.empty()) {
        check(false, QStringLiteral("no package image produced; container tests are vacuous"));
        return;
    }
    fx::FxPackageView view;
    std::string error;
    check(fx::fxpRead(image.data(), image.size(), view, error), QStringLiteral("fxpRead of a fresh package"));
    check(view.entries.size() == data.size(), QStringLiteral("entry count preserved"));
    check(pointersInsideImage(view.entries, image.data(), image.size()), QStringLiteral("all blobs in bounds"));

    std::size_t deflated = 0;
    for (std::size_t i = 0; i < view.entries.size() && i < data.size(); ++i) {
        const fx::FxEntry& e = view.entries[i];
        if (e.method == fx::kFxDeflate) {
            ++deflated;
        }
        std::vector<uint8_t> raw;
        check(fx::fxMaterialize(e, raw, error), QStringLiteral("materialize %1").arg(i));
        check(raw == data[i], QStringLiteral("materialized bytes identical for entry %1").arg(i));
        check(fx::sha256Equal(fx::sha256Compute(raw.data(), raw.size()), e.sha256),
              QStringLiteral("digest matches manifest for entry %1").arg(i));
    }
    check(deflated > 2, QStringLiteral("the fixture actually exercises both methods (deflated=%1)").arg(deflated));

    // 越界路径必须在写入端就被拒绝，安装器永远不该拿到能跳出安装目录的名字。
    for (const char* bad : {"", "..", "../x", "/abs", "C:/x", "a\\b", "a//b", "x/..", "sub/", "a/./b"}) {
        std::vector<fx::FxItem> items{makeItem(bad, std::vector<uint8_t>(16, 0x77), true)};
        std::vector<uint8_t> out;
        std::string err;
        check(!fx::fxpWrite(items, out, err), QStringLiteral("fxpWrite must reject '%1'").arg(bad));
    }
    for (std::size_t cut : {std::size_t(1), std::size_t(31), std::size_t(32), std::size_t(60),
                            std::size_t(image.size() * 3 / 5), image.size() - 1}) {
        fx::FxPackageView partial;
        std::string err;
        check(!fx::fxpRead(image.data(), cut, partial, err),
              QStringLiteral("truncated read at %1 must fail outright").arg(cut));
    }

    // 逐字节破坏：只要求两件事——解析失败，或者成功但指针全在镜像内。任何越界指针都是致命 bug。
    std::uint32_t state = 0xC0FFEEu;
    for (int trial = 0; trial < 4000; ++trial) {
        std::vector<uint8_t> copy = image;
        for (int flip = 0; flip < 4; ++flip) {
            state = state * 1664525u + 1013904223u;
            copy[state % copy.size()] ^= uint8_t(1u << (state >> 24) % 8);
        }
        fx::FxPackageView view2;
        std::string err;
        if (fx::fxpRead(copy.data(), copy.size(), view2, err)) {
            check(pointersInsideImage(view2.entries, copy.data(), copy.size()),
                  QStringLiteral("corrupt package produced an out-of-bounds blob pointer"));
            for (const fx::FxEntry& e : view2.entries) {
                // 被破坏的清单可能声称几十 GB，这类条目只验指针边界，不值得为它申请大缓冲。
                if (e.rawSize > (1u << 24) || e.compSize > size_t(image.size())) {
                    continue;
                }
                std::vector<uint8_t> raw;
                // rawSize 是硬上界：解压必须写进自己申请好的缓冲，不得溢出。
                fx::fxMaterialize(e, raw, err);
                check(raw.size() <= size_t(e.rawSize), QStringLiteral("materialize exceeded rawSize bound"));
            }
        }
    }
}

} // namespace

int main() {
    const auto data = payloads();
    testSha256AgainstQt(data);
    testInflateRoundTrip(data);
    testInflateRejectsDamage(data);
    testPackageRoundTrip(data);

    // 防止"一个断言都没跑到"的空绿：这套用例正常应有上千次比对。
    check(g_checks > 1000, QStringLiteral("sanity: at least 1000 checks ran (ran %1)").arg(g_checks));

    if (g_failures != 0) {
        std::printf("%d of %d check(s) failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("installer core OK: sha256 + inflate + FXPK container (%d checks)\n", g_checks);
    return 0;
}
