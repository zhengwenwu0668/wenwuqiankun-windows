// 由安卓版 app/src/main/res/drawable/ic_taiji_foreground.xml 的同一套几何生成 Windows 图标。
// 只用 Qt 光栅化 + 手写 ICO 容器，不引入第三方库；产物 win/res/feixing.ico 提交进仓库，
// 链接期由 win/res/app.rc 编入 exe。
//
// 重新生成：
//   cmake --build <build-dir> --target fx_icongen
//   <build-dir>/fx_icongen.exe win/res/feixing.ico

#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QVector>

#include <initializer_list>

namespace {

// 108 视口内：圆心 (54,54)，大圆 R=33，两个 R/2=16.5 的半圆构成 S 曲线，眼 R/4=8.25。
constexpr qreal kCenter = 54.0;
constexpr qreal kRadius = 33.0;

QImage taijiImage(int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    // 太极圆占图标 92%，与安卓自适应图标里前景相对安全区的比例一致。
    const qreal scale = 0.92 * size / (2 * kRadius);
    painter.translate(size / 2.0, size / 2.0);
    painter.scale(scale, scale);
    painter.translate(-kCenter, -kCenter);

    const QRectF big(kCenter - kRadius, kCenter - kRadius, 2 * kRadius, 2 * kRadius);
    const QRectF upper(kCenter - kRadius / 2.0, kCenter - kRadius, kRadius, kRadius);
    const QRectF lower(kCenter - kRadius / 2.0, kCenter, kRadius, kRadius);

    // 黑鱼 = 右半大圆 + 下半弧左凸 + 上半弧右凸；白鱼走同一条 S 曲线，只把外弧换成左半大圆。
    QPainterPath black;
    black.moveTo(kCenter, kCenter - kRadius);
    black.arcTo(big, 90, -180);
    black.arcTo(lower, -90, -180);
    black.arcTo(upper, -90, 180);
    black.closeSubpath();

    QPainterPath white;
    white.moveTo(kCenter, kCenter - kRadius);
    white.arcTo(big, 90, 180);
    white.arcTo(lower, -90, -180);
    white.arcTo(upper, -90, 180);
    white.closeSubpath();

    painter.fillPath(black, QColor(Qt::black));
    painter.fillPath(white, QColor(Qt::white));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Qt::black)); // 阴眼：上半区点黑
    painter.drawEllipse(QPointF(kCenter, kCenter - kRadius / 2.0), kRadius / 4.0, kRadius / 4.0);
    painter.setBrush(QColor(Qt::white)); // 阳眼：下半区点白
    painter.drawEllipse(QPointF(kCenter, kCenter + kRadius / 2.0), kRadius / 4.0, kRadius / 4.0);
    return image;
}

// 一个 32 位 BGRA 的 ICO 图像项：BITMAPINFOHEADER（高度含 AND 掩码）+ 自底向上像素 + 全 0 AND 掩码。
QByteArray icoEntry(const QImage &source) {
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int width = image.width();
    const int height = image.height();
    const int maskStride = ((width + 31) / 32) * 4;

    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << qint32(40) << qint32(width) << qint32(height * 2) << qint16(1) << qint16(32) << qint32(0)
           << qint32(width * height * 4 + maskStride * height) << qint32(0) << qint32(0) << qint32(0) << qint32(0);

    for (int y = height - 1; y >= 0; --y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            stream << line[x]; // 小端写出即 BGRA
        }
    }
    out.append(QByteArray(maskStride * height, '\0'));
    return out;
}

bool writeIco(const QString &filePath, std::initializer_list<int> sizes) {
    QVector<QByteArray> entries;
    QVector<int> pixelSizes;
    for (int size : sizes) {
        entries.append(icoEntry(taijiImage(size)));
        pixelSizes.append(size);
    }

    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << qint16(0) << qint16(1) << static_cast<qint16>(entries.size());
    quint32 offset = 6 + static_cast<quint32>(entries.size()) * 16;
    for (int i = 0; i < entries.size(); ++i) {
        const int size = pixelSizes.at(i);
        // 单字节的宽高字段里 256 写 0，真实尺寸由位图头给出。
        const quint8 field = size >= 256 ? 0 : static_cast<quint8>(size);
        stream << field << field << quint8(0) << quint8(0) << qint16(1) << qint16(32)
               << quint32(entries.at(i).size()) << offset;
        offset += static_cast<quint32>(entries.at(i).size());
    }
    for (const QByteArray &entry : entries) {
        out.append(entry);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("ico open failed: %s", qPrintable(filePath));
        return false;
    }
    return file.write(out) == out.size();
}

} // namespace

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const QString path = app.arguments().size() > 1 ? app.arguments().at(1) : QStringLiteral("feixing.ico");
    return writeIco(path, {16, 20, 24, 32, 40, 48, 64, 256}) ? 0 : 1;
}
