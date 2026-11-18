// FXPK 打包工具（开发侧，不进产品）：把 staging 目录打成一个载荷文件供 setup.exe 内嵌。
// 压缩用 Qt 自带的 zlib（qCompress），摘要用 Qt 的 QCryptographicHash——安装端是另一套独立实现，
// 两边对不上就会在安装时报错，所以这里不需要"自己验证自己"。
//
//   fx_pkgtool.exe <out.fxpkg> <payloadRoot>
//   fx_pkgtool.exe --verify <file.fxpkg>

#include "installer/FxInflate.h"
#include "installer/FxPackage.h"

#include <QByteArray>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void sha256Of(const uint8_t* data, std::size_t len, uint8_t out[32]) {
    const QByteArray digest = QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(data), int(len)),
                                                      QCryptographicHash::Sha256);
    std::memcpy(out, digest.constData(), 32);
}

std::string local(const QString& s) {
    return s.toStdString();
}

QStringList collectFiles(const QString& root) {
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        out.append(QDir(root).relativeFilePath(it.filePath()));
    }
    out.sort(); // 排序保证同一目录两次打包得到同一个字节序列
    return out;
}

bool loadFile(const QString& path, std::vector<uint8_t>& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::printf("cannot open %s: %s\n", local(path).c_str(), local(file.errorString()).c_str());
        return false;
    }
    const QByteArray data = file.readAll();
    bytes.assign(reinterpret_cast<const uint8_t*>(data.constData()),
                 reinterpret_cast<const uint8_t*>(data.constData()) + data.size());
    return true;
}

bool pack(const QString& packagePath, const QString& root) {
    std::vector<fx::FxItem> items;
    quint64 rawTotal = 0;
    quint64 compTotal = 0;

    for (const QString& rel : collectFiles(root)) {
        std::vector<uint8_t> raw;
        if (!loadFile(root + QLatin1Char('/') + rel, raw)) {
            return false;
        }
        rawTotal += raw.size();

        fx::FxItem item;
        item.path = local(rel);
        item.method = fx::kFxStore;
        item.content = raw;
        item.rawSize = raw.size();
        // 摘要覆盖最终落盘内容；这里用 Qt 的实现，安装端用自带的另一套独立实现核对。
        sha256Of(raw.data(), raw.size(), item.digest);

        if (!raw.empty()) {
            const QByteArray ba(reinterpret_cast<const char*>(raw.data()), int(raw.size()));
            const QByteArray packed = qCompress(ba, 9);
            const uint8_t* zlib = reinterpret_cast<const uint8_t*>(packed.constData()) + 4;
            const std::size_t zlibLen = std::size_t(packed.size()) - 4;
            std::size_t offset = 0;
            // zlib = 2 字节头 + raw deflate + 4 字节 adler32；载荷只留 raw deflate，且只在真变小时用。
            if (packed.size() >= 10 && fx::stripZlibHeader(zlib, zlibLen, offset) && zlibLen > offset + 4
                && zlibLen - offset - 4 < raw.size()) {
                item.method = fx::kFxDeflate;
                item.content.assign(zlib + offset, zlib + zlibLen - 4);
            }
        }

        compTotal += item.content.size();
        items.push_back(std::move(item));
    }

    std::vector<uint8_t> image;
    std::string error;
    if (!fx::fxpWrite(items, image, error)) {
        std::printf("pack failed: %s\n", error.c_str());
        return false;
    }

    QFile out(packagePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::printf("cannot write %s: %s\n", local(packagePath).c_str(), local(out.errorString()).c_str());
        return false;
    }
    if (out.write(reinterpret_cast<const char*>(image.data()), qint64(image.size())) != qint64(image.size())) {
        std::printf("short write on %s\n", local(packagePath).c_str());
        return false;
    }

    std::printf("packed %zu file(s): raw=%llu compressed=%llu image=%llu\n", items.size(),
                (unsigned long long)rawTotal, (unsigned long long)compTotal, (unsigned long long)image.size());
    return true;
}

bool verify(const QString& packagePath) {
    QFile in(packagePath);
    if (!in.open(QIODevice::ReadOnly)) {
        std::printf("cannot open %s\n", local(packagePath).c_str());
        return false;
    }
    const QByteArray image = in.readAll();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(image.constData());

    fx::FxPackageView view;
    std::string error;
    if (!fx::fxpRead(bytes, std::size_t(image.size()), view, error)) {
        std::printf("verify failed at parse: %s\n", error.c_str());
        return false;
    }

    std::size_t bad = 0;
    for (const fx::FxEntry& entry : view.entries) {
        std::vector<uint8_t> raw;
        if (!fx::fxMaterialize(entry, raw, error)) {
            std::printf("%s: %s\n", entry.path.c_str(), error.c_str());
            ++bad;
            continue;
        }
        uint8_t digest[32]{};
        sha256Of(raw.data(), raw.size(), digest);
        if (std::memcmp(digest, entry.sha256, 32) != 0) {
            std::printf("%s: digest mismatch\n", entry.path.c_str());
            ++bad;
        }
    }
    std::printf("verified %zu entr(y/ies), %zu bad\n", view.entries.size(), bad);
    return bad == 0;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FXPK packer"));
    parser.addHelpOption();
    QCommandLineOption verifyOption(QStringLiteral("verify"), QStringLiteral("verify an existing package"));
    parser.addOption(verifyOption);
    parser.addPositionalArgument(QStringLiteral("package"), QStringLiteral("output .fxpkg path"));
    parser.addPositionalArgument(QStringLiteral("root"), QStringLiteral("payload directory to pack"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (parser.isSet(verifyOption)) {
        if (args.size() != 1) {
            std::printf("usage: fx_pkgtool --verify <file.fxpkg>\n");
            return 2;
        }
        return verify(args.at(0)) ? 0 : 1;
    }
    if (args.size() != 2) {
        std::printf("usage: fx_pkgtool <out.fxpkg> <payloadRoot>\n");
        return 2;
    }
    return pack(args.at(0), args.at(1)) ? 0 : 1;
}
