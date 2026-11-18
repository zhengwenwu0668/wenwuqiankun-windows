#include "ui/TintedCheckBox.h"

#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace {

constexpr int kSlotSize = 24;   // 触点槽边长，对应 Compose Modifier.size(24.dp)
constexpr int kBoxSize = 18;    // 可视方框边长

} // namespace

TintedCheckBox::TintedCheckBox(const QString &text, const QColor &checkedColor, QWidget *parent)
    : QCheckBox(text, parent), checkedColor_(checkedColor) {
    setCursor(Qt::PointingHandCursor);
}

QSize TintedCheckBox::sizeHint() const {
    const QFontMetricsF metrics(font());
    return QSize(kSlotSize + static_cast<int>(std::ceil(metrics.horizontalAdvance(text()))) + 4, kSlotSize);
}

void TintedCheckBox::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal boxOffset = (kSlotSize - kBoxSize) / 2.0;
    const QRectF box(boxOffset, static_cast<qreal>(rect().center().y()) - kBoxSize / 2.0, kBoxSize, kBoxSize);

    if (isChecked()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(checkedColor_);
        painter.drawRoundedRect(box, 3.0, 3.0);

        QPen check(QColor(Qt::white));
        check.setWidthF(2.0);
        check.setCapStyle(Qt::RoundCap);
        check.setJoinStyle(Qt::RoundJoin);
        painter.setPen(check);
        QPolygonF mark;
        mark << QPointF(box.left() + 3.6, box.top() + 9.2) << QPointF(box.left() + 7.2, box.top() + 12.8)
             << QPointF(box.left() + 14.4, box.top() + 5.4);
        painter.drawPolyline(mark);
    } else {
        QPen outline(QColor(0x49, 0x45, 0x4F));
        outline.setWidthF(2.0);
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box.adjusted(1.0, 1.0, -1.0, -1.0), 2.5, 2.5);
    }

    painter.setPen(palette().windowText().color());
    painter.setFont(font());
    const QRectF textRect(kSlotSize, 0, static_cast<qreal>(width()) - kSlotSize, static_cast<qreal>(height()));
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
}
