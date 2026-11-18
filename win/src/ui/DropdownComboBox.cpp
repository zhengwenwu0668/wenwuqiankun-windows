#include "ui/DropdownComboBox.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOptionComboBox>

#include <algorithm>

namespace {

// Material3 Icons.Filled.ArrowDropDown：viewport 24，路径 M7,10 l5,5 l5,-5 z，
// 即图标盒内水平居中、宽 10 高 5 的实心三角；展开时换成 ArrowDropUp（尖端朝上）。
constexpr qreal kTriangleHalfWidth = 5.0;
constexpr qreal kTriangleHalfHeight = 2.5;
constexpr qreal kIconBoxWidth = 24.0;
// 图标盒右沿距选框内边 12dp：元运/流年/流月取 OutlinedTextField 的 contentPadding(end=12)，
// 兼向取显式 padding(end=12.dp)，两者与第 1 行箭头垂直对齐（规格 6.5）。
constexpr qreal kContentEndPadding = 12.0;
// TrailingIcon 未覆写 tint，取 Material3 浅色主题的 onSurfaceVariant。
const QColor kTrailingIconColor(0x49, 0x45, 0x4F);

} // namespace

DropdownComboBox::DropdownComboBox(ArrowPlacement placement, QWidget *parent)
    : QComboBox(parent), placement_(placement) {}

void DropdownComboBox::showPopup() {
    // 弹层容器是懒创建的，每次打开前补挂事件过滤器，用 Show/Hide 同步箭头朝向与锚点。
    if (QWidget *container = view()->parentWidget()) {
        if (!filterInstalled_) {
            container->installEventFilter(this);
            filterInstalled_ = true;
        }
    }
    QComboBox::showPopup(); // 自带嵌套事件循环，返回时弹层已关闭
}

bool DropdownComboBox::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type type = event->type();
    if (type != QEvent::Show && type != QEvent::Hide) {
        return QComboBox::eventFilter(watched, event);
    }

    popupOpen_ = type == QEvent::Show;
    update();
    if (popupOpen_) {
        // Qt 默认把当前项叠在选框上（原生下拉的习惯），Material3 的 ExposedDropdownMenu
        // 锚在选框下沿、左上角对齐，这里改回手机版的位置。
        if (auto *container = qobject_cast<QWidget *>(watched)) {
            container->move(mapToGlobal(QPoint(0, height())));
        }
    }
    return QComboBox::eventFilter(watched, event);
}

qreal DropdownComboBox::arrowCenterX() const {
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QStyle *style = this->style();

    if (placement_ == ArrowPlacement::kAfterText) {
        const QRect textRect = style->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
        const qreal textWidth = std::min<qreal>(fontMetrics().horizontalAdvance(currentText()), textRect.width());
        return textRect.left() + textWidth + kIconBoxWidth / 2.0;
    }

    const QRect arrowRect = style->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
    return static_cast<qreal>(arrowRect.center().x()) - kContentEndPadding;
}

void DropdownComboBox::paintEvent(QPaintEvent *event) {
    QComboBox::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kTrailingIconColor);

    const qreal cx = arrowCenterX();
    const qreal cy = height() / 2.0;
    const qreal baseY = popupOpen_ ? cy + kTriangleHalfHeight : cy - kTriangleHalfHeight;
    const qreal tipY = popupOpen_ ? cy - kTriangleHalfHeight : cy + kTriangleHalfHeight;

    QPolygonF triangle;
    triangle << QPointF(cx - kTriangleHalfWidth, baseY) << QPointF(cx + kTriangleHalfWidth, baseY) << QPointF(cx, tipY);
    painter.drawPolygon(triangle);
}
