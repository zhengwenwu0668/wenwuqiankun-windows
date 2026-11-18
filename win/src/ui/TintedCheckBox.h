#pragma once

#include <QCheckBox>
#include <QColor>

// Material3 勾选框的 PC 等价体：24px 触点槽内画 18px 圆角方框，勾选时用指定色实心填充并叠白勾。
// 流年用红、流月用绿，与九宫年/月星同色（规格 6.5 第 4 行）。
class TintedCheckBox final : public QCheckBox {
public:
    TintedCheckBox(const QString &text, const QColor &checkedColor, QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor checkedColor_;
};
