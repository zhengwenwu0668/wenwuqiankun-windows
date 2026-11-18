cmake_minimum_required(VERSION 3.22)

project(feixing_win
        VERSION 1.0.0
        DESCRIPTION "玄空飞星排盘 PC 版 (Xuan Kong Fei Xing)"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FX_BUILD_TESTS "Build the acceptance self-check gate" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

if(WIN32)
  enable_language(RC) # exe 图标资源
endif()

# 必须用 msvcrt 版 MinGW（本机为 D:/twdl/toolchain/Tools/mingw1310_64）编译：
# Qt 6.8.3 win64_mingw 的 DLL 链接 msvcrt.dll，而 D:/twdl/toolchain/mingw64 是 ucrt 版。
# 两者各自一套堆，Qt 分配、我们内联释放的 QString 会触发 RtlFreeHeap 非法地址而崩溃。

# 安装器核心：SHA-256 + raw DEFLATE + 载荷容器，纯 C++ 不链 Qt。
# 打包端（Qt）与安装端（Win32）共用同一份格式定义，正确性靠 tests/installcore_test.cpp 兜。
add_library(fx_installcore STATIC
    src/installer/FxSha256.cpp
    src/installer/FxInflate.cpp
    src/installer/FxPackage.cpp)
target_include_directories(fx_installcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# domain 层与 Qt 无关：验收门禁直接链接它，跑起来不需要 Qt DLL 在 PATH 上。
add_library(fx_core STATIC
    src/domain/Constants.cpp
    src/domain/FeixingCalculator.cpp
    src/domain/YearMonthCalculator.cpp
    src/domain/LichunTable.cpp)
target_include_directories(fx_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# exe 图标：res/feixing.ico 是提交进仓库的成品，由 fx_icongen 按 Android 的 ic_taiji_foreground.xml
# 几何生成。要重画太极时单独构建该工具再覆盖 ico，主构建不依赖它。
add_executable(fx_icongen EXCLUDE_FROM_ALL src/tools/icongen.cpp)
target_link_libraries(fx_icongen PRIVATE Qt6::Core Qt6::Gui)

# 载荷打包工具：同样是开发侧工具，EXCLUDE_FROM_ALL，只在打安装包脚本里显式构建。
add_executable(fx_pkgtool EXCLUDE_FROM_ALL src/tools/pkgtool.cpp)
target_link_libraries(fx_pkgtool PRIVATE fx_installcore Qt6::Core)
set(FX_ICO "${CMAKE_CURRENT_SOURCE_DIR}/res/feixing.ico")
configure_file(res/app.rc.in ${CMAKE_BINARY_DIR}/app.rc @ONLY)

qt_add_executable(feixing_win
    src/main.cpp
    src/platform/WinMainShim.cpp
    src/ui/MainWindow.cpp
    src/ui/LuopanWidget.cpp
    src/ui/DropdownComboBox.cpp
    src/ui/TintedCheckBox.cpp)
if(WIN32)
  target_sources(feixing_win PRIVATE ${CMAKE_BINARY_DIR}/app.rc)
endif()
target_include_directories(feixing_win PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(feixing_win PRIVATE fx_core Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
  # Debug 保留控制台便于看告警；Release 走纯窗口程序。
  # 必须用 $<IF:>：$<$<CONFIG:Debug>:OFF> 在 Release 下求值为空串，CMake 会当成未设置而退回控制台子系统。
  set_target_properties(feixing_win PROPERTIES WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,OFF,ON>)
endif()

if(MINGW)
  # See src/platform/WinMainShim.cpp: Qt's own entry point needs msvcrt's __argc.
  set_target_properties(feixing_win PROPERTIES qt_no_entrypoint ON)
endif()

# 与 Android 的单 flavor 对应：桌面只显示文武乾坤，产物名固定。
set_target_properties(feixing_win PROPERTIES OUTPUT_NAME wenwuqiankun)

# ---------------- 安装包 ----------------
# setup.exe 走纯 Win32：不链 Qt，自带 SHA-256 与 DEFLATE，载荷以 RT_RCDATA 内嵌，
# 所以双击就能装，目标机上不需要任何运行库。产品身份集中在 fxproduct.h.in 一处。
set(FX_APP_NAME "文武乾坤")
set(FX_APP_ID "wenwuqiankun")
set(FX_EXE_NAME "wenwuqiankun.exe")
set(FX_PUBLISHER "郑文武")
set(FX_PUBLISHER_ASCII "Zheng Wenwu")
set(FX_VERSION "${PROJECT_VERSION}")
string(REPLACE "." "," FX_VERSION_COMMAS "${PROJECT_VERSION}")
set(FX_VERSION_NUMERIC "${FX_VERSION_COMMAS},0")
set(FX_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/res/installer.manifest")
set(FX_PAYLOAD "${CMAKE_BINARY_DIR}/${FX_APP_ID}.fxpkg")
set(FX_SETUP_OUTPUT "${FX_APP_ID}-setup-${FX_VERSION}")

configure_file(src/installer/fxproduct.h.in ${CMAKE_BINARY_DIR}/generated/fxproduct.h @ONLY)
configure_file(res/setup.rc.in ${CMAKE_BINARY_DIR}/setup.rc @ONLY)
configure_file(res/uninstall.rc.in ${CMAKE_BINARY_DIR}/uninstall.rc @ONLY)

# 装盘/卸载的实际动作，只依赖系统库；与可移植的 fx_installcore 分层，便于单测前者。
add_library(fx_installer STATIC
    src/installer/FxWinUtil.cpp
    src/installer/FxInstallOps.cpp)
target_link_libraries(fx_installer PUBLIC fx_installcore)
target_include_directories(fx_installer PUBLIC ${CMAKE_BINARY_DIR}/generated)

set(FX_WIN_LIBS user32 gdi32 comctl32 shell32 ole32 advapi32)

add_executable(fx_uninstaller EXCLUDE_FROM_ALL src/installer/uninstall_main.cpp)
target_sources(fx_uninstaller PRIVATE ${CMAKE_BINARY_DIR}/uninstall.rc)
target_link_libraries(fx_uninstaller PRIVATE fx_installer ${FX_WIN_LIBS})
set_target_properties(fx_uninstaller PROPERTIES OUTPUT_NAME uninstall WIN32_EXECUTABLE ON)

# fx_setup 要读 ${FX_PAYLOAD}，fx_uninstaller 要先进载荷，两者都不进默认目标：
# 顺序由 build_installer.ps1 显式控制，缺文件时 windres 会直接报出来。
add_executable(fx_setup EXCLUDE_FROM_ALL src/installer/setup_main.cpp)
target_sources(fx_setup PRIVATE ${CMAKE_BINARY_DIR}/setup.rc)
# setup.rc 里 RCDATA 引用的是 .fxpkg 的内容，但 CMake 只看得到 setup.rc 本身：
# 不声明这条依赖，重新打包之后 ninja 只重链不重编 RC，装进 exe 的还是旧载荷。
set_source_files_properties(${CMAKE_BINARY_DIR}/setup.rc PROPERTIES OBJECT_DEPENDS ${FX_PAYLOAD})
target_link_libraries(fx_setup PRIVATE fx_installer ${FX_WIN_LIBS})
set_target_properties(fx_setup PROPERTIES OUTPUT_NAME ${FX_SETUP_OUTPUT} WIN32_EXECUTABLE ON)

if(MINGW)
  # -municode：入口取 wWinMain；-static：libgcc/libstdc++ 全静态，产物只有一个 exe。
  foreach(_fx_installer_target fx_setup fx_uninstaller)
    target_compile_options(${_fx_installer_target} PRIVATE -municode)
    target_link_options(${_fx_installer_target} PRIVATE -municode -static)
  endforeach()
endif()

if(FX_BUILD_TESTS)
  enable_testing()
  add_executable(selfcheck tests/selfcheck.cpp)
  target_link_libraries(selfcheck PRIVATE fx_core)
  add_test(NAME feixing_acceptance COMMAND selfcheck)

  # 参照物是 Qt 自带的 zlib 与 QCryptographicHash，所以这条测试必须链 Qt。
  add_executable(installcore_test tests/installcore_test.cpp)
  target_link_libraries(installcore_test PRIVATE fx_installcore Qt6::Core)
  add_test(NAME install_core COMMAND installcore_test)
  get_target_property(_fx_qtcore_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
  get_filename_component(_fx_qt_bin "${_fx_qtcore_loc}" DIRECTORY)
  get_filename_component(_fx_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set_property(TEST install_core PROPERTY ENVIRONMENT_MODIFICATION
               "PATH=path_list_prepend:${_fx_qt_bin};PATH=path_list_prepend:${_fx_mingw_bin}")
endif()
