#include <QApplication>
#include <QFont>
#include <QStringList>
#include <QTimer>

#include "ui/MainWindow.h"

namespace {

// 与安卓版「系统默认中文字体、禁止内嵌字体」一致：取 Windows 微软雅黑，缺失时交给 Qt 字体匹配。
QFont appFont() {
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei"), QStringLiteral("Noto Sans CJK SC"), QStringLiteral("SimSun")});
    font.setPixelSize(16); // 基准字号；参数区与绘图区的实际字号按绘图区边长派生
    return font;
}

} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setFont(appFont());

    MainWindow window;

    // 出图与状态注入仅供截图校对使用：
    //   --shot <png>   保存整窗截图后退出
    //   --dump <txt>   保存布局/状态转储（UTF-8）
    //   --state k=v,.. 见 MainWindow::applyState
    //   --size WxH     窗口尺寸，用于验证「绘图区边长 = 窗口宽」的派生缩放
    //   --popup <name> 打开 yun/zuo/jian/year/month 下拉后再出图，用于校对弹层与箭头
    QString shotPath;
    QString dumpPath;
    QString stateSpec;
    QString sizeSpec;
    QString popupName;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--shot") && i + 1 < args.size()) {
            shotPath = args.at(++i);
        } else if (args.at(i) == QLatin1String("--dump") && i + 1 < args.size()) {
            dumpPath = args.at(++i);
        } else if (args.at(i) == QLatin1String("--state") && i + 1 < args.size()) {
            stateSpec = args.at(++i);
        } else if (args.at(i) == QLatin1String("--size") && i + 1 < args.size()) {
            sizeSpec = args.at(++i);
        } else if (args.at(i) == QLatin1String("--popup") && i + 1 < args.size()) {
            popupName = args.at(++i);
        }
    }

    if (!stateSpec.isEmpty()) {
        window.applyState(stateSpec);
    }
    if (!sizeSpec.isEmpty()) {
        const QStringList wh = sizeSpec.split(QLatin1Char('x'));
        if (wh.size() == 2) {
            window.resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
    }
    window.show();

    if (!shotPath.isEmpty() || !dumpPath.isEmpty() || !popupName.isEmpty()) {
        QTimer::singleShot(300, &window, [&window, shotPath, dumpPath, popupName] {
            bool finished = false;
            const auto grabAndQuit = [&] {
                if (finished) {
                    return;
                }
                finished = true;
                if (!dumpPath.isEmpty()) {
                    window.dumpLayout(dumpPath);
                }
                if (!shotPath.isEmpty()) {
                    window.saveScreenshot(shotPath);
                }
                QApplication::quit();
            };
            if (popupName.isEmpty()) {
                grabAndQuit();
                return;
            }
            // showPopup 自带嵌套事件循环会阻塞，所以先把出图排进队列，再打开弹层。
            QTimer::singleShot(250, &window, grabAndQuit);
            window.openPopupForShot(popupName);
            grabAndQuit();
        });
    }

    return app.exec();
}
