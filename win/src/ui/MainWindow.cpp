#include "ui/MainWindow.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpacerItem>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <Qt>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>

#include "domain/Constants.h"
#include "domain/FeixingCalculator.h"
#include "domain/YearMonthCalculator.h"
#include "ui/DropdownComboBox.h"
#include "ui/LuopanWidget.h"
#include "ui/TintedCheckBox.h"

namespace {

constexpr int kFieldHeight = 40;   // 选框/按钮统一高度（规格 6.1）
constexpr int kRowGap = 8;         // 参数区行间距、标题与参数区间距、参数区与绘图区间距
constexpr int kSideMargin = 8;     // 参数区左右内边距
constexpr int kDegreeGapBase = 12; // 第 3 行间隔的固定部分，也是第 4 行左右两半的间隔
constexpr int kTopMargin = 8;      // 顶部留白（对应 statusBarsPadding + padding(top = 8.dp)）

QString appName() {
    return QString::fromUtf8("文武乾坤");
}

QString fromDomain(const std::string &text) {
    return QString::fromStdString(text);
}

QFont sizedFont(const QFont &base, int pixelSize, qreal letterSpacing = 0.0) {
    QFont font(base);
    font.setPixelSize(std::max(1, pixelSize));
    font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    return font;
}

// 选框描边统一中性灰 #BDBDBD（focused 与 unfocused 同色），圆角对应 Material3 的 4dp 外形。
const char *kStyleSheet = R"*(
QWidget { background-color: #FFFBFE; color: #1C1B1F; }
QLabel { background: transparent; }
QComboBox, QFrame#pairFrame, QFrame#fieldFrame {
    border: 1px solid #BDBDBD;
    border-radius: 4px;
    background: transparent;
}
QComboBox { padding: 2px 12px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox#zuoCombo, QComboBox#jianCombo, QLineEdit#bareEdit { border: none; border-radius: 0px; }
QComboBox#zuoCombo { padding: 2px 2px 2px 10px; }
QComboBox#jianCombo { padding: 2px 22px 2px 0px; }
QLineEdit#bareEdit { padding: 2px 0px; }
QPushButton#computeButton {
    background-color: #4CAF50;
    color: #FFFFFF;
    border: none;
    border-radius: 4px;
}
QLabel#toast {
    background-color: rgba(32, 31, 35, 235);
    color: #FFFFFF;
    border-radius: 8px;
}
)*";

// Android 的 DropdownMenuItem 用 Modifier.height() 定高，Qt QSS 只有 min/max，两者都给才等价。
// 弹层规则整体挂在 view 上：Windows 默认的蓝色选中行在 Material3 里不存在，
// 用 secondaryContainer 同族的浅灰 #E3E2E6 代替，文字保持 onSurface。
QString popupStyle(int itemHeight) {
    return QStringLiteral(
               "QAbstractItemView {"
               " background-color: #FFFBFE;"
               " border: 1px solid #BDBDBD;"
               " selection-background-color: #E3E2E6;"
               " selection-color: #1C1B1F;"
               " outline: none;"
               " }"
               " QAbstractItemView::item { min-height: %1px; max-height: %1px; }"
               " QAbstractItemView::item:selected { background-color: #E3E2E6; color: #1C1B1F; }")
        .arg(itemHeight);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    buildUi();
    resize(420, 700);
    relayoutScale();
    // 初始高度贴合内容：参数区 + 与窗口等宽的正方形绘图区，不留空白尾巴。
    resize(width(), chromeHeight() + luopanSide_);
    setWindowTitle(appName());
}

void MainWindow::buildUi() {
    setObjectName("root");
    setStyleSheet(QLatin1String(kStyleSheet));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(0, kTopMargin, 0, 0);
    rootLayout_->setSpacing(0);

    titleLabel_ = new QLabel(appName(), this);
    titleLabel_->setObjectName("appTitle");
    titleLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    rootLayout_->addWidget(titleLabel_);
    rootLayout_->addSpacing(kRowGap);

    params_ = new QWidget(this);
    auto *paramsLayout = new QVBoxLayout(params_);
    paramsLayout->setContentsMargins(kSideMargin, 0, kSideMargin, 0);
    paramsLayout->setSpacing(kRowGap);

    // 第 1 行：元运
    yunLabel_ = new QLabel(QString::fromUtf8("元运："), params_);
    yunLabel_->setObjectName("yunLabel");
    yunLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    yunCombo_ = new DropdownComboBox(DropdownComboBox::ArrowPlacement::kFieldEnd, params_);
    yunCombo_->setObjectName("yunCombo");
    for (int i = 1; i <= 9; ++i) {
        yunCombo_->addItem(fromDomain(fx::domain::YUN_LABELS[static_cast<std::size_t>(i - 1)]) + QStringLiteral("运（") +
                           fromDomain(fx::domain::YUN_YEARS[static_cast<std::size_t>(i - 1)]) + QStringLiteral("）"));
    }
    yunCombo_->setCurrentIndex(yun_ - 1);
    yunCombo_->setFixedHeight(kFieldHeight);
    yunCombo_->view()->setStyleSheet(popupStyle(52));
    // 弹层限高用 Android 的 heightIn(max) 换算成可见项数：500 / 52 = 9 项，正好全部显示、不出滚动条。
    // 直接给 view 设 maximumHeight 会让容器比视图高，多出来的两端留成未绘制的黑条。
    yunCombo_->setMaxVisibleItems(9);
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        row->setAlignment(Qt::AlignVCenter);
        row->addWidget(yunLabel_);
        row->addWidget(yunCombo_, 1);
        paramsLayout->addLayout(row);
    }

    // 第 2 行：坐向 + 兼向合并为一个外框，两段文字各带下拉箭头，中间留白、无分隔竖线。
    zuoLabel_ = new QLabel(QString::fromUtf8("坐向："), params_);
    zuoLabel_->setObjectName("zuoLabel");
    zuoLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    zuoJianFrame_ = new QFrame(params_);
    zuoJianFrame_->setObjectName("pairFrame");
    zuoJianFrame_->setFixedHeight(kFieldHeight);
    zuoCombo_ = new DropdownComboBox(DropdownComboBox::ArrowPlacement::kAfterText, zuoJianFrame_);
    zuoCombo_->setObjectName("zuoCombo");
    // 下拉按地盘正针序（壬山丙向起、亥山巳向止），写回的仍是 MOUNTAINS 下标。
    for (int popupIdx = 0; popupIdx < 24; ++popupIdx) {
        const int idx = (popupIdx + 23) % 24;
        zuoCombo_->addItem(fromDomain(fx::domain::ZUO_XUANG_PAIRS[static_cast<std::size_t>(idx)]));
    }
    zuoCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    zuoCombo_->setCurrentIndex((zuoIdx_ + 1) % 24);
    zuoCombo_->view()->setStyleSheet(popupStyle(kFieldHeight));
    zuoCombo_->setMaxVisibleItems(12); // 500 / 40

    jianCombo_ = new DropdownComboBox(DropdownComboBox::ArrowPlacement::kFieldEnd, zuoJianFrame_);
    jianCombo_->setObjectName("jianCombo");
    jianCombo_->insertItems(0, QStringList() << QString() << QString() << QString());
    jianCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    jianCombo_->view()->setStyleSheet(popupStyle(48)); // 未指定 height，取 Material3 默认项高
    jianCombo_->setMaxVisibleItems(3); // 兼向菜单没有 heightIn 上限，3 项全显
    refreshJianOptions();
    jianCombo_->setCurrentIndex(jianType_);
    {
        auto *pair = new QHBoxLayout(zuoJianFrame_);
        pair->setContentsMargins(1, 1, 1, 1);
        pair->setSpacing(0);
        pair->addWidget(zuoCombo_);
        pair->addStretch(1);
        pair->addSpacing(20);
        pair->addStretch(1);
        pair->addWidget(jianCombo_);

        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        row->setAlignment(Qt::AlignVCenter);
        row->addWidget(zuoLabel_);
        row->addWidget(zuoJianFrame_, 1);
        paramsLayout->addLayout(row);
    }

    // 第 3 行：坐山度数（框内右侧「度」字）+ 排盘按钮
    degLabel_ = new QLabel(QString::fromUtf8("坐山："), params_);
    degLabel_->setObjectName("degLabel");
    degLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    degFrame_ = new QFrame(params_);
    degFrame_->setObjectName("fieldFrame");
    degFrame_->setFixedHeight(kFieldHeight);
    degEdit_ = new QLineEdit(degFrame_);
    degEdit_->setObjectName("bareEdit");
    degEdit_->setText(degText_);
    degreeSuffix_ = new QLabel(QString::fromUtf8("度"), degFrame_);
    {
        auto *inner = new QHBoxLayout(degFrame_);
        inner->setContentsMargins(9, 0, 18, 0); // 数字侧留 9dp，「度」距右沿 18dp
        inner->setSpacing(0);
        inner->addWidget(degEdit_, 1);
        inner->addWidget(degreeSuffix_, 0, Qt::AlignVCenter);

        degreeGap_ = new QSpacerItem(kDegreeGapBase, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        computeButton_ = new QPushButton(QString::fromUtf8("排盘"), params_);
        computeButton_->setObjectName("computeButton");
        computeButton_->setFixedHeight(kFieldHeight);

        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        row->setAlignment(Qt::AlignVCenter);
        row->addWidget(degLabel_);
        row->addWidget(degFrame_, 1);
        row->addItem(degreeGap_);
        row->addWidget(computeButton_, 1);
        paramsLayout->addLayout(row);
    }

    // 第 4 行：流年（勾选红）+ 流月（勾选绿），左右两半等宽
    yearCheck_ = new TintedCheckBox(QString::fromUtf8("流年"), QColor(Qt::red), params_);
    monthCheck_ = new TintedCheckBox(QString::fromUtf8("流月"), QColor(0x4C, 0xAF, 0x50), params_);
    yearCombo_ = new DropdownComboBox(DropdownComboBox::ArrowPlacement::kFieldEnd, params_);
    yearCombo_->setObjectName("yearCombo");
    for (int y = 1900; y <= 2100; ++y) {
        yearCombo_->addItem(QString::number(y) + QStringLiteral("年"));
    }
    yearCombo_->setCurrentIndex(year_ - 1900);
    yearCombo_->setFixedHeight(kFieldHeight);
    yearCombo_->view()->setStyleSheet(popupStyle(48));
    yearCombo_->setMaxVisibleItems(6); // 300 / 48，与 Android 的 heightIn(max=300.dp) 等价
    monthCombo_ = new DropdownComboBox(DropdownComboBox::ArrowPlacement::kFieldEnd, params_);
    monthCombo_->setObjectName("monthCombo");
    for (int m = 1; m <= 12; ++m) {
        monthCombo_->addItem(QString::number(m) + QStringLiteral("月"));
    }
    monthCombo_->setCurrentIndex(month_ - 1);
    monthCombo_->setFixedHeight(kFieldHeight);
    monthCombo_->view()->setStyleSheet(popupStyle(48));
    monthCombo_->setMaxVisibleItems(6); // 与流年一致只显示 6 项，其余走滚动条
    {
        auto buildHalf = [this](TintedCheckBox *check, QComboBox *combo, QWidget **slotOut, const char *slotName, QBoxLayout *target) {
            auto *slot = new QWidget(params_);
            slot->setObjectName(QString::fromUtf8(slotName));
            auto *slotLayout = new QHBoxLayout(slot);
            slotLayout->setContentsMargins(0, 0, 0, 0);
            slotLayout->setSpacing(0);
            slotLayout->addWidget(check);
            auto *half = new QHBoxLayout;
            half->setContentsMargins(0, 0, 0, 0);
            half->setSpacing(0);
            half->setAlignment(Qt::AlignVCenter);
            half->addWidget(slot);
            half->addWidget(combo, 1);
            target->addLayout(half, 1);
            *slotOut = slot;
        };

        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(kDegreeGapBase);
        buildHalf(yearCheck_, yearCombo_, &yearCheckSlot_, "yearSlot", row);
        buildHalf(monthCheck_, monthCombo_, &monthCheckSlot_, "monthSlot", row);
        paramsLayout->addLayout(row);
    }

    rootLayout_->addWidget(params_);
    rootLayout_->addSpacing(kRowGap);

    luopan_ = new LuopanWidget(this);
    luopan_->setObjectName("luopan");
    rootLayout_->addWidget(luopan_, 0, Qt::AlignHCenter);
    rootLayout_->addStretch(1);

    // ---- 交互 ----
    connect(yunCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        // 元运变化：仅更新选中项，不排盘，流年/流月勾选不变。
        yun_ = index + 1;
        pushToLuopan();
    });
    connect(zuoCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int popupIdx) {
        selectZuo((popupIdx + 23) % 24);
    });
    connect(jianCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int type) { selectJian(type); });
    connect(degEdit_, &QLineEdit::textEdited, this, &MainWindow::onDegreeEdited);
    connect(computeButton_, &QPushButton::clicked, this, &MainWindow::onCompute);
    connect(yearCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        showYear_ = checked;
        if (checked && !yearMonth_.has_value()) {
            yearMonth_ = fx::domain::addYearMonth(year_, month_);
        }
        pushToLuopan();
    });
    connect(monthCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        showMonth_ = checked;
        if (checked && !yearMonth_.has_value()) {
            yearMonth_ = fx::domain::addYearMonth(year_, month_);
        }
        pushToLuopan();
    });
    connect(yearCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        year_ = 1900 + index;
        if (showYear_) {
            yearMonth_ = fx::domain::addYearMonth(year_, month_);
            pushToLuopan();
        }
    });
    connect(monthCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        month_ = index + 1;
        if (showMonth_) {
            yearMonth_ = fx::domain::addYearMonth(year_, month_);
            pushToLuopan();
        }
    });

    toast_ = new QLabel(this);
    toast_->setObjectName("toast");
    toast_->setAlignment(Qt::AlignCenter);
    toast_->setAttribute(Qt::WA_TransparentForMouseEvents);
    toast_->setHidden(true);
    toastTimer_ = new QTimer(this);
    toastTimer_->setSingleShot(true);
    toastTimer_->setInterval(2400);
    connect(toastTimer_, &QTimer::timeout, this, [this] { toast_->hide(); });

    pushToLuopan();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    relayoutScale();
}

void MainWindow::relayoutScale() {
    if (titleLabel_ == nullptr) {
        return;
    }
    // 规格 6.3：标签字号 = 九宫中心运星字号，与绘图区同源。Android 竖屏里画布边长恒等于屏宽，
    // 桌面窗口可能被高度限制，故「字号 ← 画布边长 ← 参数区高度」迭代到稳定（一般 3 次内收敛）。
    int side = std::max(120, width());
    for (int pass = 0; pass < 4; ++pass) {
        applyLabelScale(side);
        const int next = std::min(std::max(120, width()), std::max(120, height() - chromeHeight()));
        if (next == side) {
            break;
        }
        side = next;
    }
    if (side != luopanSide_) {
        luopanSide_ = side;
        luopan_->setFixedSize(luopanSide_, luopanSide_);
    }
}

void MainWindow::applyLabelScale(int canvasSide) {
    const int labelPx = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(LuopanWidget::fontPxFromCanvasSide(static_cast<float>(canvasSide))))));

    if (labelPx == appliedLabelPx_) {
        return;
    }
    appliedLabelPx_ = labelPx;

    const QFont labelFont = sizedFont(font(), labelPx, 0.25); // 与 Text 同用 bodyLarge 的 0.25 字距
    labelSlotW_ = QFontMetricsF(labelFont).horizontalAdvance(QString::fromUtf8("元运："));

    titleLabel_->setFont(sizedFont(font(), 20));
    for (QLabel *label : {yunLabel_, zuoLabel_, degLabel_}) {
        label->setFont(labelFont);
        label->setMinimumWidth(static_cast<int>(std::lround(labelSlotW_)));
    }
    yunCombo_->setFont(labelFont);
    zuoCombo_->setFont(labelFont);
    jianCombo_->setFont(labelFont);
    // 度数框只收字距与左内边距，保证末位数字不被「度」字挤掉。
    degEdit_->setFont(sizedFont(font(), labelPx, 0.0));
    degreeSuffix_->setFont(sizedFont(font(), labelPx, 0.0));
    computeButton_->setFont(sizedFont(font(), labelPx, 6.0)); // 排盘按钮 letterSpacing 6
    yearCheck_->setFont(sizedFont(font(), 15, 0.0));
    monthCheck_->setFont(sizedFont(font(), 15, 0.0));
    yearCombo_->setFont(sizedFont(font(), 14, 0.0));
    monthCombo_->setFont(sizedFont(font(), 14, 0.0));
    for (QComboBox *combo : {yunCombo_, zuoCombo_, jianCombo_, yearCombo_, monthCombo_}) {
        combo->view()->setFont(combo->font());
    }

    const int slotWidth = static_cast<int>(std::lround(labelSlotW_));
    degreeGap_->changeSize(kDegreeGapBase + slotWidth, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    yearCheckSlot_->setMinimumWidth(slotWidth);
    monthCheckSlot_->setMinimumWidth(slotWidth);
    rootLayout_->invalidate();
}

int MainWindow::chromeHeight() const {
    return kTopMargin + titleLabel_->sizeHint().height() + kRowGap + params_->sizeHint().height() + kRowGap;
}

void MainWindow::refreshJianOptions() {
    const int xiangIdx = (zuoIdx_ + 12) % 24;
    using fx::domain::MOUNTAINS;
    const QString zuoLeft = fromDomain(MOUNTAINS[static_cast<std::size_t>((zuoIdx_ + 23) % 24)]);
    const QString xiangLeft = fromDomain(MOUNTAINS[static_cast<std::size_t>((xiangIdx + 23) % 24)]);
    const QString zuoRight = fromDomain(MOUNTAINS[static_cast<std::size_t>((zuoIdx_ + 1) % 24)]);
    const QString xiangRight = fromDomain(MOUNTAINS[static_cast<std::size_t>((xiangIdx + 1) % 24)]);
    jianCombo_->setItemText(0, QString::fromUtf8("正向"));
    jianCombo_->setItemText(1, QString::fromUtf8("兼") + zuoLeft + xiangLeft);
    jianCombo_->setItemText(2, QString::fromUtf8("兼") + zuoRight + xiangRight);
}

void MainWindow::selectZuo(int zuoIdx) {
    zuoIdx_ = zuoIdx;
    jianType_ = 0;
    degText_ = QString::number(zuoIdx * 15) + QStringLiteral(".0");
    degEdit_->setText(degText_);
    zuoCombo_->setCurrentIndex((zuoIdx + 1) % 24);
    refreshJianOptions();
    jianCombo_->setCurrentIndex(jianType_);
    pushToLuopan();
}

void MainWindow::selectJian(int type) {
    jianType_ = type;
    const int center = zuoIdx_ * 15;
    int degree = center;
    if (type == 1) {
        degree = ((center - 6) % 360 + 360) % 360;
    } else if (type == 2) {
        degree = (center + 6) % 360;
    }
    degText_ = QString::number(degree) + QStringLiteral(".0");
    degEdit_->setText(degText_);
    jianCombo_->setCurrentIndex(type);
    pushToLuopan();
}

void MainWindow::onDegreeEdited(const QString &rawText) {
    // 只接受数字与小数点；度数框保留用户输入的原始值，不回写成中心度/兼向度，避免打断输入。
    QString filtered;
    filtered.reserve(rawText.size());
    for (const QChar ch : rawText) {
        if (ch.isDigit() || ch == QLatin1Char('.')) {
            filtered.append(ch);
        }
    }
    if (filtered != rawText) {
        const int cursor = degEdit_->cursorPosition();
        degEdit_->setText(filtered);
        degEdit_->setCursorPosition(std::min(cursor, static_cast<int>(filtered.size())));
    }
    degText_ = filtered;
    syncZuoAndJianFromDegree();
}

void MainWindow::syncZuoAndJianFromDegree() {
    bool ok = false;
    const float deg = degText_.toFloat(&ok);
    if (!ok || deg < 0.0f || deg >= 360.0f) {
        return;
    }
    // 山向索引与是否兼向取 domain 的判定结果，不在 UI 重复规格 2.1 公式。
    const fx::domain::FeixingResult probe = fx::domain::computeFeixing(yun_, deg);
    const float offset = std::fmod(deg - static_cast<float>(probe.zuoIdx) * 15.0f + 540.0f, 360.0f) - 180.0f;
    zuoIdx_ = probe.zuoIdx;
    jianType_ = probe.isJian ? (offset < 0.0f ? 1 : 2) : 0;
    zuoCombo_->setCurrentIndex((zuoIdx_ + 1) % 24);
    refreshJianOptions();
    jianCombo_->setCurrentIndex(jianType_);
    pushToLuopan();
}

void MainWindow::onCompute() {
    bool ok = false;
    const float deg = degText_.toFloat(&ok);
    if (!ok) {
        showToast(QString::fromUtf8("请输入有效的度数"));
        return;
    }
    if (deg < 0.0f || deg >= 360.0f) {
        showToast(QString::fromUtf8("坐山度数必须在 0 到 360 之间"));
        return;
    }
    result_ = fx::domain::computeFeixing(yun_, deg);
    yearMonth_ = fx::domain::addYearMonth(year_, month_); // 保留 showYear/showMonth，不重置勾选
    pushToLuopan();
}

void MainWindow::pushToLuopan() {
    luopan_->setContent(result_, yearMonth_, showYear_, showMonth_, zuoIdx_);
}

void MainWindow::showToast(const QString &text) {
    toast_->setText(text);
    toast_->adjustSize();
    toast_->move((width() - toast_->width()) / 2, height() - toast_->height() - 48);
    toast_->show();
    toast_->raise();
    toastTimer_->start();
}

void MainWindow::applyState(const QString &spec) {
    const QStringList pairs = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const QStringList kv = pair.split(QLatin1Char('='));
        if (kv.size() != 2) {
            continue;
        }
        const QString key = kv.at(0).trimmed();
        const QString value = kv.at(1).trimmed();
        bool ok = false;
        const int number = value.toInt(&ok);
        if (key == QLatin1String("yun") && ok) {
            yun_ = number;
            yunCombo_->setCurrentIndex(yun_ - 1);
        } else if (key == QLatin1String("zuoIdx") && ok) {
            selectZuo(number);
        } else if (key == QLatin1String("jianType") && ok) {
            selectJian(number);
        } else if (key == QLatin1String("deg")) {
            degText_ = value;
            degEdit_->setText(value);
            syncZuoAndJianFromDegree();
        } else if (key == QLatin1String("year") && ok) {
            year_ = number;
            yearCombo_->setCurrentIndex(year_ - 1900);
        } else if (key == QLatin1String("month") && ok) {
            month_ = number;
            monthCombo_->setCurrentIndex(month_ - 1);
        } else if (key == QLatin1String("showYear")) {
            yearCheck_->setChecked(value != QLatin1String("0"));
        } else if (key == QLatin1String("showMonth")) {
            monthCheck_->setChecked(value != QLatin1String("0"));
        } else if (key == QLatin1String("compute")) {
            if (value != QLatin1String("0")) {
                onCompute();
            }
        }
    }
    pushToLuopan();
}

void MainWindow::saveScreenshot(const QString &filePath) {
    // 先记录弹层再 grab()：grab 会触发一次绘制，之后弹层状态就不一定还在原样了。
    QComboBox *popupOwner = visiblePopupOwner();
    QPixmap pixmap = grab();
    if (popupOwner) {
        // 弹层是独立顶层窗口，grab() 只画本窗。把弹层离屏渲染后贴回选框下沿，
        // 得到用户实际看到的合成图（本机没有可见桌面，屏幕抓取拿不到它）。
        QPainter painter(&pixmap);
        painter.drawPixmap(popupOwner->mapTo(this, QPoint(0, popupOwner->height())),
                           popupOwner->view()->parentWidget()->grab());
    }
    if (!pixmap.save(filePath)) {
        qWarning("screenshot save failed: %s", qPrintable(filePath));
    }
}

QComboBox *MainWindow::comboByName(const QString &name) const {
    if (name == QLatin1String("yun")) return yunCombo_;
    if (name == QLatin1String("zuo")) return zuoCombo_;
    if (name == QLatin1String("jian")) return jianCombo_;
    if (name == QLatin1String("year")) return yearCombo_;
    if (name == QLatin1String("month")) return monthCombo_;
    return nullptr;
}

QComboBox *MainWindow::visiblePopupOwner() const {
    for (QComboBox *combo : {yunCombo_, zuoCombo_, jianCombo_, yearCombo_, monthCombo_}) {
        // 列表视图只在弹层打开期间可见。
        if (combo && combo->view()->isVisible()) {
            return combo;
        }
    }
    return nullptr;
}

void MainWindow::openPopupForShot(const QString &name) {
    if (QComboBox *combo = comboByName(name)) {
        combo->showPopup();
    }
}

// 出图校对用：把规格 6.3/6.5 关心的左沿、槽宽、绘图区边长写成 UTF-8 文本，
// 走 stdout 会被 Windows 控制台代码页转码，中文项会变成乱码。
void MainWindow::dumpLayout(const QString &filePath) const {
    QStringList lines;
    lines << QStringLiteral("window w=%1 h=%2  labelPx=%3 labelSlotW=%4 luopanSide=%5")
                 .arg(width()).arg(height()).arg(appliedLabelPx_).arg(labelSlotW_, 0, 'f', 2).arg(luopanSide_);
    lines << QStringLiteral("state yun=%1 zuoIdx=%2 jianType=%3 deg=%4 year=%5 month=%6 showYear=%7 showMonth=%8 computed=%9")
                 .arg(yun_).arg(zuoIdx_).arg(jianType_).arg(degText_).arg(year_).arg(month_)
                 .arg(showYear_).arg(showMonth_).arg(result_.has_value());
    lines << QStringLiteral("shown yun=%1 zuo=%2 jian=%3")
                 .arg(yunCombo_->currentText(), zuoCombo_->currentText(), jianCombo_->currentText());

    const auto row = [this, &lines](std::initializer_list<const QWidget *> widgets) {
        QString line;
        for (const QWidget *w : widgets) {
            const QPoint at = w->mapTo(this, QPoint(0, 0));
            line += QStringLiteral("  %1[%2,%3 %4x%5]").arg(w->objectName()).arg(at.x()).arg(at.y()).arg(w->width()).arg(w->height());
        }
        lines << line;
    };
    row({titleLabel_});
    row({yunLabel_, yunCombo_});
    row({zuoLabel_, zuoJianFrame_, zuoCombo_, jianCombo_});
    row({degLabel_, degFrame_, computeButton_});
    row({yearCheckSlot_, yearCombo_, monthCheckSlot_, monthCombo_});
    row({luopan_});

    // 弹层是顶层窗口，截图抓没抓到、高度是否超出容器，都只在这行里看得见。
    for (const QComboBox *combo : {yunCombo_, zuoCombo_, jianCombo_, yearCombo_, monthCombo_}) {
        const QWidget *view = combo->view();
        if (!view->isVisible()) {
            continue;
        }
        const QWidget *container = view->parentWidget();
        lines << QStringLiteral("popup %1 view=%2x%3 container=%4x%5 visible=%6")
                     .arg(combo->objectName())
                     .arg(view->width()).arg(view->height())
                     .arg(container ? container->width() : -1).arg(container ? container->height() : -1)
                     .arg(container && container->isVisible());
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("dump open failed: %s", qPrintable(filePath));
        return;
    }
    file.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
}

