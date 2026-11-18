#include "ui/LuopanWidget.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QSizeF>
#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "domain/Constants.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kNoArcLimit = std::numeric_limits<float>::infinity();

// 0..359° 的罗盘方向查表：先按 double 取弧度再收窄成 float，与逐次调用取值一致。
const std::array<float, 360> &compassSinTable() {
    static const std::array<float, 360> table = [] {
        std::array<float, 360> values{};
        for (int d = 0; d < 360; ++d) {
            values[static_cast<std::size_t>(d)] = static_cast<float>(std::sin(static_cast<double>(d) * kPi / 180.0));
        }
        return values;
    }();
    return table;
}

const std::array<float, 360> &compassCosTable() {
    static const std::array<float, 360> table = [] {
        std::array<float, 360> values{};
        for (int d = 0; d < 360; ++d) {
            values[static_cast<std::size_t>(d)] = static_cast<float>(std::cos(static_cast<double>(d) * kPi / 180.0));
        }
        return values;
    }();
    return table;
}

struct MeasuredText {
    qreal width = 0.0;
    qreal height = 0.0;
    qreal ascent = 0.0;
};

// 同一 (文字, 字号, 字距) 的测量结果复用：勾选流年/流月引起的重绘不再重新测量。
// 缓存按 logical px 建键，Qt 已在绘制时自行处理设备像素比，故无需随 DPI 重建。
class LabelCache {
public:
    MeasuredText measure(const QFont &font, const QString &text) {
        const QString key = QStringLiteral("%1#%2#%3").arg(font.pixelSize()).arg(font.letterSpacing()).arg(text);
        const auto cached = cache_.find(key);
        if (cached != cache_.end()) {
            return cached.value();
        }
        const QFontMetricsF metrics(font);
        const MeasuredText measured{metrics.horizontalAdvance(text), metrics.height(), metrics.ascent()};
        cache_.insert(key, measured);
        return measured;
    }

private:
    QHash<QString, MeasuredText> cache_;
};

LabelCache &labelCache() {
    static LabelCache cache;
    return cache;
}

QFont labelFont(const QFont &base, float fontPx) {
    QFont font(base);
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(static_cast<double>(fontPx)))));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    return font;
}

// 0° 在正下方，顺时针为正：x = cx + r·sin d，y = cy − r·cos d。
QPointF compassToCanvas(float cx, float cy, float r, float compassDeg) {
    const float rad = static_cast<float>(static_cast<double>(compassDeg) * kPi / 180.0);
    return QPointF(cx + r * std::sin(rad), cy - r * std::cos(rad));
}

// 整数度走查表，取值与逐次计算相同。
QPointF compassToCanvas(float cx, float cy, float r, int compassDeg) {
    return QPointF(cx + r * compassSinTable()[static_cast<std::size_t>(compassDeg)],
                   cy - r * compassCosTable()[static_cast<std::size_t>(compassDeg)]);
}

// 角度数字：文字底边原点，与刻度线原点相差 180°，故画在中心上方 radius 处再整体旋转。
void drawRotatedLabel(QPainter &painter,
                      const QFont &base,
                      float compassAngle,
                      float cx,
                      float cy,
                      float radius,
                      const QString &text,
                      float fontPx,
                      const QColor &color,
                      float arcLimit = kNoArcLimit) {
    QFont font = labelFont(base, fontPx);
    MeasuredText measured = labelCache().measure(font, text);
    const qreal threshold = static_cast<qreal>(arcLimit) * 0.95;
    if (measured.width > threshold) {
        font = labelFont(base, static_cast<float>(fontPx * (threshold / measured.width)));
        measured = labelCache().measure(font, text);
    }
    painter.save();
    painter.translate(cx, cy);
    painter.rotate(static_cast<double>(compassAngle));
    painter.translate(-cx, -cy);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QPointF(cx - measured.width / 2.0, cy + static_cast<qreal>(radius) - measured.height / 2.0 + measured.ascent),
                     text);
    painter.restore();
}

void drawCenteredLabel(QPainter &painter,
                       const QFont &base,
                       float x,
                       float y,
                       const QString &text,
                       float fontPx,
                       const QColor &color) {
    const QFont font = labelFont(base, fontPx);
    const MeasuredText measured = labelCache().measure(font, text);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QPointF(static_cast<qreal>(x) - measured.width / 2.0, static_cast<qreal>(y) - measured.height / 2.0 + measured.ascent),
                     text);
}

QString fromDomain(const std::string &text) {
    return QString::fromStdString(text);
}

// 箭尾（坐山侧）：偏移量按 R0 定尺，贴在半径 R 的环边外侧，环带放大时箭头本身不变大。
void drawZuoArrow(QPainter &painter, const QPen &pen, float r, float r0, float cx, float cy, float compassDeg) {
    constexpr float deltaDeg = 1.24f;
    painter.save();
    painter.translate(cx, cy);
    painter.rotate(static_cast<double>(compassDeg));
    painter.translate(-cx, -cy);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal shaftTop = static_cast<qreal>(cy - (r + 0.019f * r0));
    painter.drawLine(QPointF(cx, static_cast<qreal>(cy - r)), QPointF(cx, shaftTop));

    const qreal wingY = static_cast<qreal>(cy - (r + 0.0625f * r0));
    const float wingDx = 1.0625f * r0 * static_cast<float>(std::sin(static_cast<double>(deltaDeg) * kPi / 180.0));
    const qreal notchY = static_cast<qreal>(cy - (r + 0.044f * r0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Qt::black));
    QPolygonF leftWing;
    leftWing << QPointF(cx, shaftTop) << QPointF(static_cast<qreal>(cx) - wingDx, wingY) << QPointF(cx, notchY);
    painter.drawPolygon(leftWing);
    QPolygonF rightWing;
    rightWing << QPointF(cx, shaftTop) << QPointF(static_cast<qreal>(cx) + wingDx, wingY) << QPointF(cx, notchY);
    painter.drawPolygon(rightWing);

    painter.restore();
}

// 箭头（向首侧）：同箭尾，偏移量按 R0 定尺。
void drawXiangArrow(QPainter &painter, const QPen &pen, float r, float r0, float cx, float cy, float compassDeg) {
    constexpr float deltaDeg = 1.05f;
    painter.save();
    painter.translate(cx, cy);
    painter.rotate(static_cast<double>(compassDeg));
    painter.translate(-cx, -cy);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal tipY = static_cast<qreal>(cy - (r + 0.0625f * r0));
    painter.drawLine(QPointF(cx, static_cast<qreal>(cy - r)), QPointF(cx, tipY));

    const float baseDx = 1.025f * r0 * static_cast<float>(std::sin(static_cast<double>(deltaDeg) * kPi / 180.0));
    const qreal baseY = static_cast<qreal>(cy - (r + 0.025f * r0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Qt::black));
    QPolygonF head;
    head << QPointF(cx, tipY) << QPointF(static_cast<qreal>(cx) - baseDx, baseY) << QPointF(static_cast<qreal>(cx) + baseDx, baseY);
    painter.drawPolygon(head);

    painter.restore();
}

} // namespace

LuopanWidget::LuopanWidget(QWidget *parent) : QWidget(parent) {
    // 正方形由父容器按窗口宽度定尺，这里只声明水平方向可伸展。
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumSize(120, 120);
}

void LuopanWidget::setContent(std::optional<fx::domain::FeixingResult> result,
                              std::optional<fx::domain::YearMonthResult> yearMonth,
                              bool showYear,
                              bool showMonth,
                              int zuoIdx) {
    result_ = std::move(result);
    yearMonth_ = std::move(yearMonth);
    showYear_ = showYear;
    showMonth_ = showMonth;
    zuoIdx_ = zuoIdx;
    update();
}

float LuopanWidget::fontPxFromCanvasSide(float sidePx) {
    const float r0 = 0.40f * sidePx;
    const float a = 0.825f * r0 / std::sqrt(2.0f);
    const float step = 2.0f * a / 3.0f;
    return step * 0.35f;
}

QSize LuopanWidget::sizeHint() const {
    return QSize(420, 420);
}

QSize LuopanWidget::minimumSizeHint() const {
    return QSize(120, 120);
}

void LuopanWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1 清空（白底）
    painter.fillRect(rect(), QColor(Qt::white));

    const float s = std::min(static_cast<float>(width()), static_cast<float>(height()));
    if (s <= 0.0f) {
        return;
    }
    const float cx = static_cast<float>(width()) / 2.0f;
    const float cy = static_cast<float>(height()) / 2.0f;
    // R 为环带基准半径；R0 只用于定住箭头尺寸，使箭头不随环带放大。
    const float r0 = 0.40f * s;
    const float r = 0.465f * s;
    const float lineWidth = 0.6f;

    QPen pen(QColor(Qt::black));
    pen.setWidthF(static_cast<qreal>(lineWidth));
    pen.setCapStyle(Qt::FlatCap); // Compose drawLine 默认 Butt 端帽
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setBrush(QColor(Qt::black));

    // 2 三层圆
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), static_cast<qreal>(r), static_cast<qreal>(r));
    painter.drawEllipse(QPointF(cx, cy), static_cast<qreal>(0.925f * r), static_cast<qreal>(0.925f * r));
    painter.drawEllipse(QPointF(cx, cy), static_cast<qreal>(0.825f * r), static_cast<qreal>(0.825f * r));

    // 3 360° 刻度线
    for (int d = 0; d < 360; ++d) {
        // 10° 刻度压到 0.015R：腾出的径向空间给放大后的度盘数字。
        const float tickLen = (d % 10 == 0) ? 0.01500f * r : ((d % 5 == 0) ? 0.05000f * r : 0.03125f * r);
        painter.drawLine(compassToCanvas(cx, cy, r, d), compassToCanvas(cx, cy, r - tickLen, d));
    }

    // 4 每 10° 的黑点与角度数字
    const float availArc = 9.5f * static_cast<float>(kPi / 180.0) * 0.946875f * r;
    for (int d = 0; d < 360; d += 10) {
        const QPointF dotCenter = compassToCanvas(cx, cy, 0.984375f * r, d);
        const float dotRadius = 0.00625f * r;
        QPolygonF dot;
        for (int i = 0; i < 12; ++i) {
            const double angle = static_cast<double>(i * 30) * kPi / 180.0;
            dot << QPointF(dotCenter.x() + static_cast<qreal>(dotRadius) * static_cast<float>(std::cos(angle)),
                           dotCenter.y() + static_cast<qreal>(dotRadius) * static_cast<float>(std::sin(angle)));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::black));
        painter.drawPolygon(dot);

        drawRotatedLabel(painter, font(), static_cast<float>(d), cx, cy, 0.946875f * r, QString::number(d),
                         0.0677f * r, QColor(Qt::black), availArc);
    }

    // 5 二十四山分界线
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (int k = 0; k < 24; ++k) {
        const float boundaryAngle = static_cast<float>(k * 15) + 7.5f;
        painter.drawLine(compassToCanvas(cx, cy, 0.825f * r, boundaryAngle), compassToCanvas(cx, cy, 0.925f * r, boundaryAngle));
    }

    // 6 山名
    for (int j = 0; j < 24; ++j) {
        const float angle = static_cast<float>(j * 15);
        drawRotatedLabel(painter, font(), angle, cx, cy, 0.875f * r, fromDomain(fx::domain::MOUNTAINS[static_cast<std::size_t>(j)]),
                         s * 0.0389f, QColor(Qt::black));
    }

    // 7 九宫格线
    const float a = 0.825f * r / std::sqrt(2.0f);
    const float step = 2.0f * a / 3.0f;
    const qreal gridLeft = static_cast<qreal>(cx - a);
    const qreal gridTop = static_cast<qreal>(cy - a);
    const qreal gridRight = static_cast<qreal>(cx + a);
    const qreal gridBottom = static_cast<qreal>(cy + a);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(gridLeft, gridTop), QPointF(gridRight, gridTop));
    painter.drawLine(QPointF(gridRight, gridTop), QPointF(gridRight, gridBottom));
    painter.drawLine(QPointF(gridRight, gridBottom), QPointF(gridLeft, gridBottom));
    painter.drawLine(QPointF(gridLeft, gridBottom), QPointF(gridLeft, gridTop));
    for (int i = 1; i <= 2; ++i) {
        const qreal y = gridTop + i * step;
        painter.drawLine(QPointF(gridLeft, y), QPointF(gridRight, y));
        const qreal x = gridLeft + i * step;
        painter.drawLine(QPointF(x, gridTop), QPointF(x, gridBottom));
    }

    // 8 运/山/向星，9 红年星，10 绿月星
    // 角星星位的 Y 偏移按 Canvas Y 轴向下取：山/向星在上两角，年/月星在下两角。
    if (result_.has_value()) {
        const QColor monthColor(0x4C, 0xAF, 0x50);
        for (int idx = 0; idx < 9; ++idx) {
            const int row = idx / 3;
            const int col = idx % 3;
            const float cellCx = cx + static_cast<float>(col - 1) * step;
            const float cellCy = cy + static_cast<float>(row - 1) * step;
            const fx::domain::FeixingResult &result = *result_;

            const QString yunChar = fromDomain(fx::domain::YUN_LABELS[static_cast<std::size_t>(result.yunPan[idx] - 1)]);
            drawCenteredLabel(painter, font(), cellCx, cellCy, yunChar, step * 0.35f, QColor(Qt::black));
            drawCenteredLabel(painter, font(), cellCx - step * 0.32f, cellCy - step * 0.32f, QString::number(result.shanPan[idx]),
                              step * 0.25f, QColor(Qt::black));
            drawCenteredLabel(painter, font(), cellCx + step * 0.32f, cellCy - step * 0.32f, QString::number(result.xiangPan[idx]),
                              step * 0.25f, QColor(Qt::black));

            if (showYear_ && yearMonth_.has_value()) {
                drawCenteredLabel(painter, font(), cellCx - step * 0.32f, cellCy + step * 0.32f,
                                  QString::number((*yearMonth_).yearPan[idx]), step * 0.25f, QColor(Qt::red));
            }
            if (showMonth_ && yearMonth_.has_value()) {
                drawCenteredLabel(painter, font(), cellCx + step * 0.32f, cellCy + step * 0.32f,
                                  QString::number((*yearMonth_).monthPan[idx]), step * 0.25f, monthColor);
            }
        }
    }

    // 11 坐山燕尾箭，12 向首箭头
    // 跟随排盘实际度数：未排盘时回退到下拉选中的中心度；两侧角度参数互换（原点差 180°）。
    const float zuoDeg = result_.has_value() ? result_->zuoDeg : static_cast<float>(zuoIdx_ * 15);
    const float xiangDeg = std::fmod(zuoDeg + 180.0f, 360.0f);
    drawZuoArrow(painter, pen, r, r0, cx, cy, xiangDeg);
    drawXiangArrow(painter, pen, r, r0, cx, cy, zuoDeg);
}
