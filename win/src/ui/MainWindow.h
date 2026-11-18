#pragma once

#include <QWidget>

#include <optional>
#include <QString>

#include "domain/FeixingCalculator.h"
#include "domain/YearMonthCalculator.h"

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSpacerItem;
class QTimer;
class QVBoxLayout;

class LuopanWidget;
class TintedCheckBox;

// 主界面 = 参数区 4 行 + 正方形绘图区，逐行对应 Android 版 ui/LuopanScreen.kt。
// 默认值同样取规格 6.2：九运 / 壬山丙向 / 正向 / 345.0 度 / 2020 年 6 月 / 年月星不显示。
class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 截图与自检用的状态注入，k=v 逗号分隔：yun / zuoIdx / jianType / deg / year / month / showYear / showMonth / compute。
    void applyState(const QString &spec);
    void saveScreenshot(const QString &filePath);
    void dumpLayout(const QString &filePath) const;
    // 打开指定下拉（yun/zuo/jian/year/month）；QComboBox::showPopup 自带嵌套事件循环，会阻塞到弹层关闭。
    void openPopupForShot(const QString &name);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void relayoutScale();
    void applyLabelScale(int canvasSide);
    int chromeHeight() const;

    void refreshJianOptions();
    void selectZuo(int zuoIdx);
    void selectJian(int type);
    void onDegreeEdited(const QString &rawText);
    void syncZuoAndJianFromDegree();
    void onCompute();
    void pushToLuopan();
    void showToast(const QString &text);
    QComboBox *comboByName(const QString &name) const;
    QComboBox *visiblePopupOwner() const;

    int yun_ = 9;
    int zuoIdx_ = 23;
    int jianType_ = 0;
    QString degText_ = QStringLiteral("345.0");
    int year_ = 2020;
    int month_ = 6;
    bool showYear_ = false;
    bool showMonth_ = false;

    std::optional<fx::domain::FeixingResult> result_;
    std::optional<fx::domain::YearMonthResult> yearMonth_;

    QVBoxLayout *rootLayout_ = nullptr;
    QWidget *params_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *yunLabel_ = nullptr;
    QLabel *zuoLabel_ = nullptr;
    QLabel *degLabel_ = nullptr;
    QLabel *degreeSuffix_ = nullptr;
    QComboBox *yunCombo_ = nullptr;
    QComboBox *zuoCombo_ = nullptr;
    QComboBox *jianCombo_ = nullptr;
    QComboBox *yearCombo_ = nullptr;
    QComboBox *monthCombo_ = nullptr;
    QFrame *zuoJianFrame_ = nullptr;
    QFrame *degFrame_ = nullptr;
    QLineEdit *degEdit_ = nullptr;
    QPushButton *computeButton_ = nullptr;
    TintedCheckBox *yearCheck_ = nullptr;
    TintedCheckBox *monthCheck_ = nullptr;
    QWidget *yearCheckSlot_ = nullptr;
    QWidget *monthCheckSlot_ = nullptr;
    LuopanWidget *luopan_ = nullptr;
    QSpacerItem *degreeGap_ = nullptr;
    QLabel *toast_ = nullptr;
    QTimer *toastTimer_ = nullptr;

    int appliedLabelPx_ = 0;
    qreal labelSlotW_ = 0.0;
    int luopanSide_ = 0;
};
