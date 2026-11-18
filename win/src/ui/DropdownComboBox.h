#pragma once

#include <QComboBox>

// 规格 6.5 里 5 个选框都带 ExposedDropdownMenuDefaults.TrailingIcon，Qt 的 QSS 一旦接管
// ::drop-down 就不再画原生箭头，所以这里按 Material3 的几何自绘：24px 图标盒内放 10x5 实心三角，
// 弹层展开时翻成向上三角（ArrowDropUp）。
class DropdownComboBox final : public QComboBox {
public:
    // kFieldEnd：图标盒右沿内缩 12px（元运/兼向/流年/流月，对应 contentPadding 与 padding(end=12.dp)）。
    // kAfterText：图标盒紧跟在选中文字之后（坐向用 Row(Start){Text; TrailingIcon}）。
    enum class ArrowPlacement { kFieldEnd, kAfterText };

    explicit DropdownComboBox(ArrowPlacement placement, QWidget *parent = nullptr);

    void showPopup() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    qreal arrowCenterX() const;

    ArrowPlacement placement_;
    bool popupOpen_ = false;
    bool filterInstalled_ = false;
};
