#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define INITGUID
#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include "FxInstallOps.h"

#include "FxInflate.h"
#include "FxPackage.h"
#include "FxSha256.h"
#include "FxWinUtil.h"
#include "fxproduct.h"

#include <algorithm>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>

namespace fxinst {
namespace {

constexpr std::size_t kPathLimit = 32000;
constexpr std::size_t kMinPackageBytes = 64;

bool hasControlChar(const std::wstring& text) {
    for (const wchar_t c : text) {
        if (c < 32) {
            return true;
        }
    }
    return false;
}

// path 是否落在 dir 这一棵子树里（大小写与 / \ 都不敏感）。
bool isWithin(const std::wstring& dir, const std::wstring& path) {
    auto normalize = [](std::wstring value) {
        for (auto& c : value) {
            if (c == L'/') {
                c = L'\\';
            }
        }
        while (value.size() > 3 && value.back() == L'\\') {
            value.pop_back();
        }
        return value;
    };
    const std::wstring d = normalize(dir);
    const std::wstring p = normalize(path);
    if (p.size() <= d.size() || _wcsnicmp(p.c_str(), d.c_str(), d.size()) != 0) {
        return false;
    }
    return p[d.size()] == L'\\';
}

// 载荷条目名 → 安装目录下的绝对路径。fxpRead 已经拦过一轮，这里是写盘前的最后一道闸。
bool resolveEntry(const std::wstring& root, const std::string& relPath, std::wstring& out) {
    const std::wstring wide = fxwin::toWide(relPath);
    if (wide.empty() || hasControlChar(wide) || fxwin::isAbsolute(wide)) {
        return false;
    }
    std::wstring normalized = wide;
    for (auto& c : normalized) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    out = fxwin::combine(root, normalized);
    return out.size() < kPathLimit && !hasControlChar(out);
}

std::wstring quoted(const std::wstring& path) {
    return L"\"" + path + L"\"";
}

bool readFileFully(const std::wstring& path, std::vector<uint8_t>& out) {
    // 载荷不可能超过 2 GiB，超了就是文件坏了。
    return fxwin::readFileBytes(path, out, 2ull << 30);
}

void rollbackFiles(std::vector<std::wstring>& written) {
    for (auto it = written.rbegin(); it != written.rend(); ++it) {
        fxwin::deleteFile(*it);
    }
    // 顺手清掉我们创建的子目录：非空自然删不掉，失败忽略即可。
    for (auto it = written.rbegin(); it != written.rend(); ++it) {
        RemoveDirectoryW(fxwin::parentDir(*it).c_str());
    }
}

bool writeRegistry(const std::wstring& targetDir, std::size_t totalBytes) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, FX_UNINSTALL_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    auto sz = [key](const wchar_t* name, const std::wstring& value) {
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       DWORD((value.size() + 1) * sizeof(wchar_t)));
    };
    auto dw = [key](const wchar_t* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    };

    wchar_t date[16]{};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wsprintfW(date, L"%04u%02u%02u", now.wYear, now.wMonth, now.wDay);

    const std::wstring exe = fxwin::combine(targetDir, FX_EXE_NAME);
    const std::wstring unlink = fxwin::combine(targetDir, FX_UNINSTALL_NAME);

    sz(L"DisplayName", FX_APP_NAME);
    sz(L"DisplayVersion", FX_VERSION);
    sz(L"Publisher", FX_PUBLISHER);
    sz(L"InstallLocation", targetDir);
    sz(L"InstallDate", date);
    sz(L"DisplayIcon", exe);
    sz(L"UninstallString", quoted(unlink));
    sz(L"QuietUninstallString", quoted(unlink) + L" --silent");
    dw(L"NoModify", 1); // 没有增量修改的概念，改装就是重装
    dw(L"NoRepair", 1);
    dw(L"EstimatedSize", DWORD(totalBytes / 1024));
    RegCloseKey(key);
    return true;
}

std::wstring linkFor(const GUID& folder) {
    std::wstring dir;
    if (!fxwin::knownFolder(folder, dir)) {
        return {};
    }
    return fxwin::combine(dir, std::wstring(FX_APP_NAME) + L".lnk");
}

} // namespace

bool defaultTargetDir(std::wstring& out) {
    std::wstring local;
    if (!fxwin::knownFolder(FOLDERID_LocalAppData, local)) {
        return false;
    }
    out = fxwin::combine(fxwin::combine(local, L"Programs"), FX_APP_ID);
    return !out.empty();
}

std::wstring resolveTargetDir(const std::wstring& parent) {
    if (parent.empty() || hasControlChar(parent)) {
        return parent;
    }
    std::wstring dir = parent;
    for (auto& c : dir) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    // "D:\" 归一成 "D:" 再拼一层，得到的还是 "D:\wenwuqiankun"，而不是 "D:\\…"。
    while (dir.size() > 2 && dir.back() == L'\\') {
        dir.pop_back();
    }
    const std::size_t slash = dir.find_last_of(L'\\');
    const std::wstring last = slash == std::wstring::npos ? dir : dir.substr(slash + 1);
    if (_wcsicmp(last.c_str(), FX_APP_ID) == 0) {
        return dir;
    }
    return dir + (L"\\" FX_APP_ID);
}

std::wstring startMenuLink() {
    // FOLDERID_StartMenu 是 "Start Menu" 本身，应用快捷方式要落在 FOLDERID_Programs
    // （%APPDATA%\Microsoft\Windows\Start Menu\Programs）里才会出现在开始菜单列表。
    return linkFor(FOLDERID_Programs);
}

std::wstring desktopLink() {
    return linkFor(FOLDERID_Desktop);
}

bool loadPackageImage(std::vector<uint8_t>& image, std::wstring& error) {
    // 载荷类型只能按序号 10 取：mingw 的 RT_RCDATA 是依赖 UNICODE 宏的 ANSI 形式，
    // 而字符串 "RCDATA" 在 windres 产物上枚举不到（实测 FindResourceW 直接 miss）。
    const LPCWSTR rcData = MAKEINTRESOURCEW(10);
    if (HRSRC src = FindResourceW(nullptr, MAKEINTRESOURCEW(FX_PAYLOAD_RCDATA_ID), rcData)) {
        if (HGLOBAL res = LoadResource(nullptr, src)) {
            if (const void* data = LockResource(res)) {
                const DWORD size = SizeofResource(nullptr, src);
                if (size >= kMinPackageBytes) {
                    const uint8_t* bytes = static_cast<const uint8_t*>(data);
                    image.assign(bytes, bytes + size);
                    return true;
                }
            }
        }
    }

    // 内嵌资源不在（开发期直接跑编译产物），退回读旁边的载荷文件。
    const std::wstring exe = fxwin::modulePath();
    const std::wstring sidecar = fxwin::combine(fxwin::parentDir(exe), std::wstring(FX_APP_ID) + L".fxpkg");
    if (readFileFully(sidecar, image) && image.size() >= kMinPackageBytes) {
        return true;
    }

    image.clear();
    error = L"找不到安装载荷：内嵌资源缺失，旁边也没有 " + std::wstring(FX_APP_ID) + L".fxpkg";
    return false;
}

bool install(const Options& opt, const ProgressFn& progress, std::wstring& error) {
    if (opt.targetDir.empty() || !fxwin::isAbsolute(opt.targetDir) || opt.targetDir.size() >= kPathLimit) {
        error = L"安装目录必须是一个足够长的绝对路径";
        return false;
    }
    if (opt.targetDir.size() <= 3 || hasControlChar(opt.targetDir)) {
        error = L"安装目录不能是盘根或含控制字符";
        return false;
    }

    std::vector<uint8_t> image;
    if (!loadPackageImage(image, error)) {
        return false;
    }

    fx::FxPackageView view;
    std::string detail;
    if (!fx::fxpRead(image.data(), image.size(), view, detail)) {
        error = L"载荷解析失败：" + fxwin::toWide(detail);
        return false;
    }

    if (!fxwin::createDirTree(opt.targetDir)) {
        error = L"无法创建安装目录：" + opt.targetDir;
        return false;
    }

    std::vector<std::wstring> written;
    std::size_t totalBytes = 0;
    const std::size_t total = view.entries.size();
    for (std::size_t i = 0; i < total; ++i) {
        const fx::FxEntry& entry = view.entries[i];
        std::wstring dest;
        if (!resolveEntry(opt.targetDir, entry.path, dest)) {
            error = L"载荷里有非法文件名：" + fxwin::toWide(entry.path);
            rollbackFiles(written);
            return false;
        }

        std::vector<uint8_t> raw;
        if (!fx::fxMaterialize(entry, raw, detail)) {
            error = L"解压失败：" + fxwin::toWide(detail);
            rollbackFiles(written);
            return false;
        }
        if (!fx::sha256Equal(fx::sha256Compute(raw.data(), raw.size()), entry.sha256)) {
            error = L"校验和不匹配，安装包可能已损坏：" + fxwin::toWide(entry.path);
            rollbackFiles(written);
            return false;
        }
        if (!fxwin::createDirTree(fxwin::parentDir(dest)) || !fxwin::writeFileBytes(dest, raw.data(), raw.size())) {
            error = L"写入失败：" + dest;
            rollbackFiles(written);
            return false;
        }

        totalBytes += raw.size();
        written.push_back(dest);
        if (progress) {
            progress(i + 1, total, fxwin::toWide(entry.path));
        }
    }

    const std::wstring exe = fxwin::combine(opt.targetDir, FX_EXE_NAME);
    const std::wstring workDir = opt.targetDir;

    fxwin::ComGuard com;
    const std::wstring startLink = startMenuLink();
    if (startLink.empty() || !fxwin::createShortcut(startLink, exe, workDir, exe, FX_APP_NAME)) {
        error = L"无法创建开始菜单快捷方式";
        rollbackFiles(written);
        return false;
    }
    if (opt.desktopShortcut) {
        // 桌面路径可能被重定向或不存在，装不上快捷方式不该毁掉一次安装。
        if (const std::wstring link = desktopLink(); !link.empty()) {
            fxwin::createShortcut(link, exe, workDir, exe, FX_APP_NAME);
        }
    }

    if (!writeRegistry(opt.targetDir, totalBytes)) {
        error = L"无法写入卸载信息（注册表 HKCU）";
        rollbackFiles(written);
        return false;
    }

    if (progress) {
        progress(total, total, std::wstring(FX_APP_NAME) + L" 已安装");
    }
    return true;
}

bool uninstall(const std::wstring& targetDir, const std::wstring& keepFile, std::wstring& error) {
    if (targetDir.empty() || !fxwin::isAbsolute(targetDir) || targetDir.size() <= 3) {
        error = L"卸载目录非法：" + targetDir;
        return false;
    }
    // 主程序不在了就别删：这个目录可能已经被用户挪作他用。
    if (!fxwin::fileExists(fxwin::combine(targetDir, FX_EXE_NAME))) {
        error = std::wstring(FX_APP_NAME) + L" 的主程序不在这个目录，已停止删除：" + targetDir;
        return false;
    }

    std::wstring kept;
    if (!keepFile.empty() && isWithin(targetDir, keepFile)) {
        kept = keepFile;
    }

    // 先删文件，文件删不掉就原样退出，不留半截状态。
    if (!fxwin::removeTreeExcept(targetDir, kept)) {
        error = L"部分文件没能删除（可能有程序正在运行）：" + targetDir;
        return false;
    }

    // 快捷方式只是普通文件，删除它们不需要 COM。
    for (const std::wstring& link : {startMenuLink(), desktopLink()}) {
        if (!link.empty()) {
            fxwin::deleteFile(link);
        }
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, FX_UNINSTALL_KEY);
    return true;
}

} // namespace fxinst
