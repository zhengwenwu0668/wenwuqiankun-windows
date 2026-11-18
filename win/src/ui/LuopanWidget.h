#pragma once

#include <QWidget>

#include <optional>

#include "domain/FeixingCalculator.h"
#include "domain/YearMonthCalculator.h"

// 罗盘绘图区：三层同心圆（360° 刻度层 / 二十四山层 / 九宫格）+ 星位 + 坐山燕尾箭与向首箭头。
// 全部几何量按规格第七节以 R 的倍数表达，S 取控件短边，与 Compose Canvas 的 size.minDimension 同义。
class LuopanWidget final : public QWidget {
    Q_OBJECT

public:
    explicit LuopanWidget(QWidget *parent = nullptr);

    void setContent(std::optional<fx::domain::FeixingResult> result,
                    std::optional<fx::domain::YearMonthResult> yearMonth,
                    bool showYear,
                    bool showMonth,
                    int zuoIdx);

    // 九宫格中心运星字号（px），供参数区沿用，保证参数区版面与九宫格字号同源。
    static float fontPxFromCanvasSide(float sidePx);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::optional<fx::domain::FeixingResult> result_;
    std::optional<fx::domain::YearMonthResult> yearMonth_;
    bool showYear_ = false;
    bool showMonth_ = false;
    int zuoIdx_ = 23;
};
