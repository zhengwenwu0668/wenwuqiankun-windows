#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>

#include "FxInstallOps.h"
#include "FxWinUtil.h"
#include "fxproduct.h"

#include <string>

namespace {

std::wstring registryTarget() {
    wchar_t buf[4096]{};
    DWORD type = 0;
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, FX_UNINSTALL_KEY, L"InstallLocation", RRF_RT_REG_SZ, &type, buf, &size)
        != ERROR_SUCCESS) {
        return {};
    }
    return buf;
}

// 卸载器在自己的安装目录里跑着，删不掉自己的镜像。做法：卸载成功后先把自己改成一个
// 带 pid 的唯一名字（运行中的 exe 允许同卷改名），再让系统 cmd 稍后删掉这个名字和空目录。
// 改名是关键：万一用户立刻重装，延迟删除也只会命中旧的那份，不会误删刚装上的 uninstall.exe。
void purgeSelf(const std::wstring& selfPath, const std::wstring& targetDir) {
    const std::wstring pending = selfPath + L"-" + std::to_wstring(GetCurrentProcessId()) + L".pending";
    fxwin::scheduleDeleteAfterExit(fxwin::moveFile(selfPath, pending) ? pending : selfPath, targetDir);
}

void report(bool ok, const std::wstring& message, const std::wstring& detail) {
    const std::wstring caption = std::wstring(FX_APP_NAME) + L" 卸载";
    MessageBoxW(nullptr, (message + L"\n\n" + detail).c_str(), caption.c_str(),
                (ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR) | MB_SETFOREGROUND);
}

int runInteractive(const std::wstring& targetDir, const std::wstring& selfPath) {
    const std::wstring caption = std::wstring(FX_APP_NAME) + L" 卸载";
    const std::wstring ask = L"确定要卸载 " + std::wstring(FX_APP_NAME) + L" 吗？\n\n安装目录：" + targetDir
                             + L"\n将删除安装目录里的程序文件、快捷方式和卸载信息，不动其他任何内容。";
    if (MessageBoxW(nullptr, ask.c_str(), caption.c_str(),
                    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND) != IDOK) {
        return 2;
    }

    std::wstring error;
    if (!fxinst::uninstall(targetDir, selfPath, error)) {
        report(false, L"卸载未完成。", error);
        return 1;
    }

    std::wstring detail = L"已卸载：" + targetDir;
    const bool inPlace = fxwin::pathsEqual(fxwin::parentDir(selfPath), targetDir);
    report(true, L"卸载完成。", detail);
    // 延迟删除要排在对话框之后：cmd 的延时是按它自己启动开始算的，
    // 用户还盯着弹窗时就排程，等于给一个还在用的文件判了刑。
    if (inPlace) {
        purgeSelf(selfPath, targetDir);
    }
    return 0;
}

int runSilent(const std::wstring& targetDir, const std::wstring& selfPath) {
    fxwin::attachConsoleUtf8();
    std::wstring error;
    if (!fxinst::uninstall(targetDir, selfPath, error)) {
        fxwin::consolePrint(error, true);
        return 1;
    }
    if (fxwin::pathsEqual(fxwin::parentDir(selfPath), targetDir)) {
        purgeSelf(selfPath, targetDir);
    }
    fxwin::consolePrint(L"已卸载：" + targetDir);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 2;
    }

    std::wstring targetArg;
    bool silent = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--silent") {
            silent = true;
        } else if (a == L"--target" && i + 1 < argc) {
            targetArg = argv[++i];
        }
    }

    const std::wstring selfPath = fxwin::modulePath();
    // 目标目录：命令行 > 注册表 > 自己所在目录，三个都够不上就什么都不动。
    std::wstring targetDir = targetArg;
    if (targetDir.empty()) {
        targetDir = registryTarget();
    }
    if (targetDir.empty()) {
        targetDir = fxwin::parentDir(selfPath);
    }

    const int rc = silent ? runSilent(targetDir, selfPath) : runInteractive(targetDir, selfPath);
    LocalFree(argv);
    return rc;
}
