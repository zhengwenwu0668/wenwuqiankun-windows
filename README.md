# 文武乾坤 · 玄空飞星排盘 PC 版（Wenwu Qiankun / Xuan Kong Fei Xing）

Qt6 C++ + QPainter 的 Windows 桌面版玄空飞星排盘。输入元运、坐向、兼向、坐山度数，绘制三层同心圆罗盘（360° 刻度、二十四山、九宫飞星），并支持叠加流年、流月紫白飞星。界面外观逐项对齐安卓版。

> 安卓版（Kotlin + Jetpack Compose）见 [zhengwenwu0668/wenwuqiankun](https://github.com/zhengwenwu0668/wenwuqiankun)，两端的领域常量、顺逆判定与五组验收用例逐条一致。

- 关键词：玄空、飞星、玄空飞星、紫白飞星、三元九运、风水、罗盘、排盘
- Keywords: xuankong, xuan kong, feixing, xuankongfeixing, flying star, feng shui, luopan, compass, zi bai, 三元九运

## 功能特性

- **玄空飞星排盘**：运盘（永远顺飞）、山星、向星，支持下卦与替卦（兼向）
- **二十四山罗盘**：三层同心圆，360° 刻度层、二十四山层、九宫格，坐山燕尾箭 + 向首实心箭头
- **流年 / 流月紫白飞星**：立春切年、逐月逆推，勾选后叠加显示红年星 / 绿月星
- **程序**：`wenwuqiankun.exe`，桌面标题与应用名「文武乾坤」，exe 内嵌太极图标

## 技术栈

- C++20 + Qt 6.8.3（Core / Gui / Widgets），零第三方依赖
- QPainter 自绘罗盘，未内嵌任何字体文件，用系统默认中文字体
- CMake + Ninja + MinGW 13.1（**必须 msvcrt 版**，见下）
- 单窗口竖排布局：上方 5 行参数区，下方正方形绘图区

## 构建

依赖本机路径（工具链独立安装，不在仓库内）：

| 组件 | 路径 |
|------|------|
| Qt | `D:/twdl/toolchain/Qt/6.8.3/mingw_64` |
| MinGW（msvcrt 版） | `D:/twdl/toolchain/Tools/mingw1310_64` |
| CMake / Ninja | `D:/twdl/toolchain/Tools/CMake_64`、`D:/twdl/toolchain/Tools/Ninja` |

```powershell
$env:PATH = "D:\twdl\toolchain\Tools\mingw1310_64\bin;D:\twdl\toolchain\Tools\Ninja;D:\twdl\toolchain\Tools\CMake_64\bin;" + $env:PATH

cmake -S win -B win/build -G Ninja -DCMAKE_BUILD_TYPE=Debug `
      -DCMAKE_CXX_COMPILER=D:/twdl/toolchain/Tools/mingw1310_64/bin/g++.exe `
      -DCMAKE_PREFIX_PATH=D:/twdl/toolchain/Qt/6.8.3/mingw_64
cmake --build win/build
```

产物：`win\build\wenwuqiankun.exe`（Debug 保留控制台便于看 Qt 告警，Release 为纯窗口程序）。

> **CRT 版本是硬约束**：Qt 6.8.3 win64_mingw 的 DLL 链接 `msvcrt.dll`。换成 ucrt 版 MinGW（如 `D:/twdl/toolchain/mingw64`）后两套堆混用，Qt 分配、内联释放的 `QString` 会在 `RtlFreeHeap` 崩溃。

## 验收门禁

领域算法（排盘数组、顺逆判定、流年/流月星数）由不依赖 Qt 的 `fx_core` 静态库承担，`ctest` 逐项精确比对五组验收用例：

```powershell
ctest --test-dir win/build --output-on-failure
```

## 独立分发包

```powershell
D:\twdl\toolchain\Qt\6.8.3\mingw_64\bin\windeployqt.exe `
  --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
  win\build-release\wenwuqiankun.exe
```

打包整个目录即为免安装分发包（`win\dist\wenwuqiankun\`），随附的 MinGW 运行时 DLL 必须同样来自 msvcrt 版工具链。

## 安装包

免安装目录适合拷给自己用；发给别人用一条命令产出的单个 setup.exe：

```powershell
powershell -ExecutionPolicy Bypass -File win\build_installer.ps1
```

依次跑 Release 构建 → `ctest` 两道门禁（排盘数组 + 安装器原语）→ windeployqt 组 staging → `fx_pkgtool` 压成 FXPK 载荷 → `fx_setup` 把载荷编成 `RT_RCDATA` 内嵌，产物落在 `win\dist\installer\wenwuqiankun-setup-<版本>.exe`（约 14 MB，目标机不需要 Qt 或 MinGW 运行库）。

安装器与卸载器都是纯 Win32 程序，不链 Qt；SHA-256 与 DEFLATE 解码在本仓库内自实现，不引入第三方库。向导里选的是**上级目录**，程序文件一律装进它下面的 `wenwuqiankun` 文件夹（默认 `%LOCALAPPDATA%\Programs\wenwuqiankun`）；卸载只删这一层文件夹，上级目录和其中的其他文件一概不动。注册表只写当前用户的卸载项，快捷方式只建一个开始菜单项，全程不需要管理员权限。`--silent`（可带 `--target <dir>`、`--desktop`）走无界面安装，`--target` 同样按"上级目录"解释，卸载器 `uninstall.exe` 也认 `--silent`。

## 项目结构

```
win/
  CMakeLists.txt
  build_installer.ps1 # 一条命令产出 setup.exe
  src/
    domain/        # Constants / LichunTable / FeixingCalculator / YearMonthCalculator（纯 C++，不依赖 Qt）
    ui/            # MainWindow（参数区）/ LuopanWidget（罗盘绘制）/ DropdownComboBox / TintedCheckBox
    platform/      # WinMainShim.cpp —— 自行接管入口，绕开 Qt 入口对 __argc 的依赖
    installer/     # SHA-256 / DEFLATE / FXPK 容器 / 安装卸载操作 + 向导与卸载器入口（纯 Win32，不链 Qt）
    tools/         # icongen.cpp（生成 res/feixing.ico）、pkgtool.cpp（打载荷）——开发侧工具，不属于主构建
  res/             # feixing.ico + app.rc.in / setup.rc.in / uninstall.rc.in / installer.manifest
  tests/           # selfcheck.cpp（验收门禁）/ installcore_test.cpp（安装器原语对拍）
```

## 图标

`win/res/feixing.ico` 是提交进仓库的成品，几何取自安卓版 `ic_taiji_foreground.xml`。需要重画时单独构建该工具再生成：

```powershell
cmake --build win/build --target fx_icongen
win\build\fx_icongen.exe win\res\feixing.ico
```

## 许可证

[MIT](LICENSE)

## 致谢

本项目为个人独立开发的玄空飞星排盘工具，欢迎 star、issue 与 PR。
