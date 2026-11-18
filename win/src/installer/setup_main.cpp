#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define INITGUID
#include <windows.h>

#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#include "FxInstallOps.h"
#include "FxWinUtil.h"
#include "fxproduct.h"

#include <string>
#include <vector>

namespace {

enum WidgetId {
    kTitle = 201,
    kDescription,
    kDirLabel,
    kDirEdit,
    kDirHint,
    kBrowse,
    kDesktopCheck,
    kProgress,
    kStatus,
    kPrimary,
    kSecondary,
};

constexpr int kClientW = 470;
constexpr int kClientH = 252;
constexpr int kMargin = 18;
constexpr const wchar_t* kWindowClass = L"wenwuqiankunSetupWizard";

struct Wizard {
    HWND self = nullptr;
    HWND title = nullptr;
    HWND description = nullptr;
    HWND dirLabel = nullptr;
    HWND dirEdit = nullptr;
    HWND dirHint = nullptr;
    HWND browse = nullptr;
    HWND desktop = nullptr;
    HWND progress = nullptr;
    HWND status = nullptr;
    HWND primary = nullptr;
    HWND secondary = nullptr;
    HFONT font = nullptr;
    HFONT heading = nullptr;
    std::wstring error;
    bool installing = false;
    bool done = false;
};

Wizard* fromHwnd(HWND hwnd) {
    return reinterpret_cast<Wizard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

int scale(HWND hwnd, int value) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi != 0 ? MulDiv(value, int(dpi), 96) : value;
}

RECT layout(HWND hwnd, int x, int y, int w, int h) {
    RECT r{scale(hwnd, x), scale(hwnd, y), scale(hwnd, x + w), scale(hwnd, y + h)};
    return r;
}

HWND makeChild(Wizard* wiz, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
               int id) {
    HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, scale(wiz->self, x),
                                 scale(wiz->self, y), scale(wiz->self, w), scale(wiz->self, h), wiz->self,
                                 reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    if (wiz->font != nullptr) {
        SendMessageW(child, WM_SETFONT, WPARAM(wiz->font), TRUE);
    }
    return child;
}

void setEnabledAll(Wizard* wiz, bool enabled) {
    const BOOL value = enabled ? TRUE : FALSE;
    for (HWND h : {wiz->dirEdit, wiz->browse, wiz->desktop, wiz->primary}) {
        EnableWindow(h, value);
    }
}

void pump(HWND hwnd) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage(int(msg.wParam));
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UpdateWindow(hwnd);
}

void reportProgress(Wizard* wiz, std::size_t done, std::size_t total, const std::wstring& current) {
    if (wiz->progress != nullptr && total != 0) {
        SendMessageW(wiz->progress, PBM_SETPOS, WPARAM(done * 100 / total), 0);
    }
    if (wiz->status != nullptr) {
        SetWindowTextW(wiz->status, current.c_str());
    }
    pump(wiz->self);
}

// 浏览框的起始位置只能靠回调设：BROWSEINFOW.pidlRoot 是"限制"而不是"从哪儿打开"，
// 一旦设了，整棵树就扎根在那个文件夹上，往上退不到「此电脑」，别的盘符也就选不到了。
int CALLBACK browseCallback(HWND hwnd, UINT msg, LPARAM, LPARAM lpData) {
    if (msg == BFFM_INITIALIZED && lpData != 0) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData); // wParam=TRUE：lParam 是路径字符串
    }
    return 0;
}

bool pickDirectory(Wizard* wiz) {
    std::wstring initial;
    const int len = GetWindowTextLengthW(wiz->dirEdit);
    std::vector<wchar_t> buf(size_t(len) + 2);
    if (GetWindowTextW(wiz->dirEdit, buf.data(), DWORD(buf.size())) > 0) {
        initial = buf.data();
    }

    // 起始位置必须是真实存在的目录，否则 SETSELECTION 不生效。框里通常是 …\wenwuqiankun，
    // 首次安装或上次被删过就不存在了，那就一层层退到第一个还在的祖先。
    std::wstring seed = initial;
    for (int guard = 0; guard < 32 && !seed.empty() && !fxwin::directoryExists(seed); ++guard) {
        const std::wstring up = fxwin::parentDir(seed);
        if (up.empty() || up == seed) {
            seed.clear();
            break;
        }
        seed = up;
    }

    wchar_t display[MAX_PATH + 1]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = wiz->self;
    bi.pszDisplayName = display;
    bi.lpszTitle = L"选择安装目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    bi.lpfn = &browseCallback;
    bi.lParam = reinterpret_cast<LPARAM>(seed.empty() ? nullptr : seed.c_str());

    const bool ok = [&] {
        PIDLIST_ABSOLUTE picked = SHBrowseForFolderW(&bi);
        if (picked == nullptr) {
            return false;
        }
        wchar_t path[MAX_PATH + 1]{};
        const bool converted = SHGetPathFromIDListW(picked, path) != FALSE;
        CoTaskMemFree(picked);
        if (converted) {
            // 选中的是上级目录，框里直接回显真正会写入的那一层，别等装完才发现多了一层。
            const std::wstring resolved = fxinst::resolveTargetDir(path);
            SetWindowTextW(wiz->dirEdit, resolved.c_str());
        }
        return converted;
    }();
    return ok;
}

void runInstall(Wizard* wiz) {
    fxinst::Options opt;
    const int len = GetWindowTextLengthW(wiz->dirEdit);
    std::vector<wchar_t> buf(size_t(len) + 2);
    GetWindowTextW(wiz->dirEdit, buf.data(), DWORD(buf.size()));
    // 手打的路径同样要补那一层，并且立刻回写：装完看到的目录必须和框里一致。
    opt.targetDir = fxinst::resolveTargetDir(buf.data());
    SetWindowTextW(wiz->dirEdit, opt.targetDir.c_str());
    opt.desktopShortcut = SendMessageW(wiz->desktop, BM_GETCHECK, 0, 0) == BST_CHECKED;

    wiz->installing = true;
    setEnabledAll(wiz, false);
    SendMessageW(wiz->progress, PBM_SETPOS, 0, 0);
    pump(wiz->self);

    // 安装跑在 UI 线程上：文件不多（二十来个），但进度条要在写盘间隙刷出来，
    // 所以每落一个文件就 pump 一次，而不是另起线程做跨线程同步。
    const bool ok = fxinst::install(opt, [&](std::size_t d, std::size_t t, const std::wstring& current) {
        reportProgress(wiz, d, t, current);
    }, wiz->error);

    wiz->installing = false;
    if (!ok) {
        SetWindowTextW(wiz->status, L"安装失败");
        setEnabledAll(wiz, true);
        MessageBoxW(wiz->self, (L"安装失败：\n" + wiz->error).c_str(), FX_APP_NAME,
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        SetWindowTextW(wiz->status, L"安装未完成，可以改个目录再试。");
        return;
    }

    wiz->done = true;
    setEnabledAll(wiz, false);
    EnableWindow(wiz->secondary, TRUE);
    SetWindowTextW(wiz->secondary, L"完成");
    SetWindowTextW(wiz->primary, L"已安装");
    SetWindowTextW(wiz->status, (std::wstring(L"安装完成：") + opt.targetDir).c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Wizard* wiz = fromHwnd(hwnd);
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CREATE:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CTLCOLORSTATIC: {
        // 让说明文字跟着窗口底色走，避免主题下出现一块灰斑。
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND:
        if (wiz == nullptr || wiz->installing) {
            return 0; // 安装过程中吃掉所有按钮消息，进度 pump 期间的重入点击不作数
        }
        // mingw 头里没有 GET_WM_COMMAND_ID，直接取 wParam 低字节的控件 ID。
        switch (LOWORD(wParam)) {
        case kBrowse:
            pickDirectory(wiz);
            return 0;
        case kPrimary:
            if (wiz->done) {
                DestroyWindow(hwnd);
            } else {
                runInstall(wiz);
            }
            return 0;
        case kSecondary:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    case WM_CLOSE:
        if (wiz != nullptr && wiz->installing) {
            return 0; // 写到一半被关掉会留下半套文件
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (wiz != nullptr) {
            if (wiz->heading != nullptr) {
                DeleteObject(wiz->heading);
            }
            if (wiz->font != nullptr) {
                DeleteObject(wiz->font);
            }
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

std::wstring installLocationFromRegistry() {
    wchar_t buf[4096]{};
    DWORD type = 0;
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, FX_UNINSTALL_KEY, L"InstallLocation", RRF_RT_REG_SZ, &type, buf, &size)
        != ERROR_SUCCESS) {
        return {};
    }
    return buf;
}

int runSilent(int argc, wchar_t** argv) {
    fxwin::attachConsoleUtf8();
    fxinst::Options opt;
    opt.targetDir = installLocationFromRegistry();
    if (opt.targetDir.empty() && !fxinst::defaultTargetDir(opt.targetDir)) {
        fxwin::consolePrint(L"无法确定安装目录", true);
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--target" && i + 1 < argc) {
            opt.targetDir = fxinst::resolveTargetDir(argv[++i]);
        } else if (a == L"--desktop") {
            opt.desktopShortcut = true;
        }
    }

    std::wstring error;
    if (!fxinst::install(opt, nullptr, error)) {
        fxwin::consolePrint(error, true);
        return 1;
    }
    fxwin::consolePrint(L"已安装：" + opt.targetDir);
    return 0;
}

int runWizard() {
    // SHBrowseForFolderW 的 BIF_NEWDIALOGSTYLE 要求线程已初始化 COM，否则直接返回失败。
    fxwin::ComGuard com;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    Wizard wiz{};
    wiz.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW headingLg = ncm.lfMessageFont;
    headingLg.lfHeight = MulDiv(headingLg.lfHeight, 15, 12);
    headingLg.lfWeight = FW_SEMIBOLD;
    wiz.heading = CreateFontIndirectW(&headingLg);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    const int width = kClientW;
    const int height = kClientH;
    wiz.self = CreateWindowExW(0, kWindowClass, (std::wstring(FX_APP_NAME) + L" 安装").c_str(),
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                               width, height, nullptr, nullptr, wc.hInstance, &wiz);
    if (wiz.self == nullptr) {
        return 1;
    }

    // 客户区尺寸要按 DPI 校正一次：非客户区差不管，多出来的高度会挤掉最后一行控件。
    RECT request{0, 0, scale(wiz.self, width), scale(wiz.self, height)};
    AdjustWindowRectExForDpi(&request, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0,
                             GetDpiForWindow(wiz.self));
    SetWindowPos(wiz.self, nullptr, 0, 0, request.right - request.left, request.bottom - request.top,
                 SWP_NOMOVE | SWP_NOZORDER);

    wiz.title = makeChild(&wiz, L"STATIC", (std::wstring(FX_APP_NAME) + L" PC 版 安装向导").c_str(), SS_LEFT, kMargin,
                          kMargin, kClientW - 2 * kMargin, 24, kTitle);
    SendMessageW(wiz.title, WM_SETFONT, WPARAM(wiz.heading), TRUE);
    wiz.description = makeChild(&wiz, L"STATIC",
                                L"安装到当前用户目录，不需要管理员权限；卸载信息会写进 HKCU。", SS_LEFT, kMargin, 50,
                                kClientW - 2 * kMargin, 34, kDescription);

    // 重复安装（升级）默认落回上次的位置；取不到 %LOCALAPPDATA% 时宁可留空让用户自己填，
    // 也不写 "." —— 相对路径会被 install() 的路径守卫拒掉，但空框比"看着能装"诚实。
    std::wstring target = installLocationFromRegistry();
    if (target.empty()) {
        fxinst::defaultTargetDir(target);
    }
    wiz.dirLabel = makeChild(&wiz, L"STATIC", L"安装到：", SS_LEFT, kMargin, 88, 70, 20, kDirLabel);
    wiz.dirEdit = makeChild(&wiz, L"EDIT", target.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, kMargin + 74, 86,
                            kClientW - 2 * kMargin - 74 - 84, 22, kDirEdit);
    wiz.browse = makeChild(&wiz, L"BUTTON", L"浏览…", BS_PUSHBUTTON | WS_TABSTOP, kClientW - kMargin - 78, 85, 78, 24,
                           kBrowse);
    wiz.dirHint = makeChild(&wiz, L"STATIC", L"程序文件会装进所选目录下的 wenwuqiankun 文件夹。", SS_LEFT, kMargin,
                            112, kClientW - 2 * kMargin, 18, kDirHint);
    wiz.desktop = makeChild(&wiz, L"BUTTON", L"同时在桌面创建快捷方式", BS_AUTOCHECKBOX | WS_TABSTOP, kMargin, 136,
                            kClientW - 2 * kMargin, 22, kDesktopCheck);
    wiz.progress = makeChild(&wiz, PROGRESS_CLASSW, L"", 0, kMargin, 168, kClientW - 2 * kMargin, 18, kProgress);
    wiz.status = makeChild(&wiz, L"STATIC", L"准备就绪", SS_LEFT, kMargin, 194, kClientW - 2 * kMargin, 20, kStatus);
    wiz.primary = makeChild(&wiz, L"BUTTON", L"安装", BS_DEFPUSHBUTTON | WS_TABSTOP, kClientW - kMargin - 170, 218, 80,
                            26, kPrimary);
    wiz.secondary = makeChild(&wiz, L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, kClientW - kMargin - 86, 218, 80,
                              26, kSecondary);

    // 窗口居中，别贴着左上角。
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT now{};
    GetWindowRect(wiz.self, &now);
    SetWindowPos(wiz.self, nullptr, (work.left + work.right) / 2 - (now.right - now.left) / 2,
                 (work.top + work.bottom) / 2 - (now.bottom - now.top) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(wiz.self, SW_SHOW);
    UpdateWindow(wiz.self);

    MSG msg{};
    for (;;) {
        const int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {
            break; // WM_QUIT，退出码在 msg.wParam 里
        }
        if (got == -1) {
            return 1; // 消息循环出错：msg 可能是上一轮的残留，不能当退出码用
        }
        if (!IsDialogMessageW(wiz.self, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return int(msg.wParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 2;
    }

    bool silent = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--silent") {
            silent = true;
        }
    }

    const int rc = silent ? runSilent(argc, argv) : runWizard();
    LocalFree(argv);
    return rc;
}
