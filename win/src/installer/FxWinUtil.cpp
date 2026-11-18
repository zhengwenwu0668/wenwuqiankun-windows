#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define INITGUID // 让 FOLDERID_* 在本编译单元里直接定义，省掉额外的 libuuid 依赖
#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>

#include "FxWinUtil.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace fxwin {

// Windows 路径大小写不敏感，且 / 与 \ 等价。注册表里的值和 GetModuleFileNameW 还给我们的
// 那份常常只差大小写，用 == 比较会把"自己就在安装目录里"判成不在，残留的卸载器就没人删了。
bool pathsEqual(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) {
        return false;
    }
    std::wstring x = a;
    std::wstring y = b;
    for (auto& c : x) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    for (auto& c : y) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    return _wcsicmp(x.c_str(), y.c_str()) == 0;
}

namespace {

constexpr std::size_t kPathLimit = 32000;

bool isSeparator(wchar_t c) {
    return c == L'\\' || c == L'/';
}

// 递归删文件；空目录顺手删掉但删不掉不算失败——里面可能正留着运行中的卸载器。
bool deleteTreeRecursive(const std::wstring& dir, const std::wstring& keepFile) {
    bool ok = true;
    WIN32_FIND_DATAW find{};
    const HANDLE handle = FindFirstFileW(combine(dir, L"*").c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        const std::wstring name = find.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring full = combine(dir, name);
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                // 链接指向别处：只断开这个入口，绝不顺着删进目标。
                if (!RemoveDirectoryW(full.c_str()) && !deleteFile(full)) {
                    ok = false;
                }
                continue;
            }
            if (!deleteTreeRecursive(full, keepFile)) {
                ok = false;
            }
            RemoveDirectoryW(full.c_str());
        } else if (!pathsEqual(full, keepFile)) {
            if (!deleteFile(full)) {
                ok = false;
            }
        }
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    return ok;
}

} // namespace

ComGuard::ComGuard() : ok_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}

ComGuard::~ComGuard() {
    if (ok_) {
        CoUninitialize();
    }
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(size_t(need), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), need) != need) {
        return {};
    }
    return out;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(size_t(need), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), out.data(), need, nullptr, nullptr) != need) {
        return {};
    }
    return out;
}

std::wstring modulePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (int round = 0; round < 8; ++round) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) {
            return std::wstring(buf.data(), n);
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return {};
        }
        buf.resize(buf.size() * 2);
    }
    return {};
}

bool knownFolder(const GUID& folderId, std::wstring& out) {
    wchar_t* raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw)) || raw == nullptr) {
        return false;
    }
    out.assign(raw);
    CoTaskMemFree(raw);
    return !out.empty() && out.size() < kPathLimit;
}

bool envPath(const wchar_t* name, std::wstring& out) {
    wchar_t buf[4096];
    const DWORD n = GetEnvironmentVariableW(name, buf, DWORD(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return false;
    }
    out.assign(buf, n);
    return true;
}

std::wstring combine(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) {
        return b;
    }
    if (!b.empty() && (isAbsolute(b) || b.front() == L'\\')) {
        return b;
    }
    if (isSeparator(a.back())) {
        return a + b;
    }
    return a + L"\\" + b;
}

std::wstring parentDir(const std::wstring& path) {
    std::size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) {
        return {};
    }
    std::wstring parent = path.substr(0, cut);
    while (parent.size() > 3 && isSeparator(parent.back())) {
        parent.pop_back();
    }
    return parent;
}

bool isAbsolute(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        return true;
    }
    return path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]);
}

bool directoryExists(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool fileExists(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool createDirTree(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_EXISTS || rc == ERROR_ALREADY_EXISTS;
}

bool writeFileBytes(const std::wstring& path, const uint8_t* data, std::size_t len) {
    if (path.empty() || path.size() >= kPathLimit) {
        return false;
    }
    if (len > DWORD(-1)) {
        return false; // 载荷里不该有这么大的单文件，出现了就拒绝而不是分段赌运气
    }
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | (len > (8u << 20) ? FILE_FLAG_WRITE_THROUGH : 0), nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    std::size_t done = 0;
    while (done < len) {
        const DWORD chunk = DWORD(std::min<std::size_t>(len - done, 1u << 22));
        DWORD written = 0;
        if (!WriteFile(handle, data + done, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        done += written;
    }
    if (!CloseHandle(handle)) {
        ok = false;
    }
    return ok;
}

bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out, std::size_t maxBytes) {
    out.clear();
    if (path.empty() || path.size() >= kPathLimit) {
        return false;
    }
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    const bool sized = GetFileSizeEx(handle, &size) != FALSE && size.QuadPart > 0
                     && std::size_t(size.QuadPart) <= maxBytes;
    if (!sized) {
        CloseHandle(handle);
        return false;
    }
    out.resize(std::size_t(size.QuadPart));
    std::size_t done = 0;
    bool ok = true;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD chunk = DWORD(std::min<std::size_t>(out.size() - done, 1u << 22));
        if (!ReadFile(handle, out.data() + done, chunk, &got, nullptr) || got == 0) {
            ok = false;
            break;
        }
        done += got;
    }
    CloseHandle(handle);
    if (!ok) {
        out.clear();
    }
    return ok;
}

bool moveFile(const std::wstring& from, const std::wstring& to) {
    if (from.empty() || to.empty()) {
        return false;
    }
    // MOVEFILE_COPY_ALLOWED 关掉：跨卷会退化成复制，那时旧文件还在，等于没改名。
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
}

void scheduleDeleteAfterExit(const std::wstring& file, const std::wstring& dir) {
    if (file.empty() && dir.empty()) {
        return;
    }
    auto quoted = [](const std::wstring& path) { return L"\"" + path + L"\""; };
    // ping 当 sleep 用：timeout 在标准输入被重定向时会直接失败。
    std::wstring line = L"cmd.exe /c ping -n 4 127.0.0.1 >nul";
    if (!file.empty()) {
        line += L" & del /q /f " + quoted(file);
    }
    if (!dir.empty()) {
        // rmdir 不带 /s：目录里只要还有别的东西就删不动，正好当最后一道保险。
        line += L" & rmdir " + quoted(dir);
    }
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                       &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void attachConsoleUtf8() {
    AttachConsole(ATTACH_PARENT_PROCESS);
    SetConsoleOutputCP(CP_UTF8);
}

void consolePrint(const std::wstring& text, bool toError) {
    FILE* stream = toError ? stderr : stdout;
    const std::string line = toUtf8(text) + "\n";
    std::fwrite(line.data(), 1, line.size(), stream);
    std::fflush(stream);
}

bool deleteFile(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (DeleteFileW(path.c_str())) {
        return true;
    }
    // 只读属性会挡住删除，清掉再试一次。
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(path.c_str()) != FALSE;
}

bool removeTree(const std::wstring& path) {
    if (!directoryExists(path) || path.size() >= kPathLimit) {
        return !directoryExists(path);
    }
    std::vector<wchar_t> from(path.begin(), path.end());
    from.push_back(L'\0');
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

bool removeTreeExcept(const std::wstring& path, const std::wstring& keepFile) {
    if (!directoryExists(path)) {
        return true;
    }
    if (keepFile.empty()) {
        return removeTree(path);
    }
    const bool ok = deleteTreeRecursive(path, keepFile);
    // 装着 keepFile 的那一层删不掉，留给延迟删除收尾。
    RemoveDirectoryW(path.c_str());
    return ok;
}

bool createShortcut(const std::wstring& linkPath, const std::wstring& target, const std::wstring& workingDir,
                    const std::wstring& iconLocation, const std::wstring& description) {
    if (linkPath.empty() || target.empty()) {
        return false;
    }
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link));
    if (FAILED(hr) || link == nullptr) {
        return false;
    }
    link->SetPath(target.c_str());
    if (!workingDir.empty()) {
        link->SetWorkingDirectory(workingDir.c_str());
    }
    if (!iconLocation.empty()) {
        link->SetIconLocation(iconLocation.c_str(), 0);
    }
    if (!description.empty()) {
        link->SetDescription(description.c_str());
    }

    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (SUCCEEDED(hr) && persist != nullptr) {
        hr = persist->Save(linkPath.c_str(), TRUE);
        persist->Release();
    }
    link->Release();
    return SUCCEEDED(hr);
}

} // namespace fxwin
