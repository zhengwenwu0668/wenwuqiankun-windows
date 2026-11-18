#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fxwin {

// 线程级 COM 初始化守卫。SHBrowseForFolder、IShellLink 这类壳层 API 都要求先初始化，
// 重复调用是安全的（按线程引用计数）。
class ComGuard {
public:
    ComGuard();
    ~ComGuard();
    ComGuard(const ComGuard&) = delete;
    ComGuard& operator=(const ComGuard&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);

// 当前可执行文件的完整路径。
std::wstring modulePath();

// 已知文件夹（CSIDL 的现代替代）：Programs 开始菜单、Desktop、LOCALAPPDATA 都从这里取。
bool knownFolder(const GUID& folderId, std::wstring& out);

// 取环境变量目录，例如 %LOCALAPPDATA%；缺失时返回 false。
bool envPath(const wchar_t* name, std::wstring& out);

std::wstring combine(const std::wstring& a, const std::wstring& b);
std::wstring parentDir(const std::wstring& path);

// 大小写与 / \ 都不敏感的路径比较（Windows 路径本来就是这两点不敏感）。
bool pathsEqual(const std::wstring& a, const std::wstring& b);
bool isAbsolute(const std::wstring& path);
bool directoryExists(const std::wstring& path);
bool fileExists(const std::wstring& path);

// 逐级创建目录；已存在视为成功。
bool createDirTree(const std::wstring& path);

// 覆盖写文件；父目录必须已存在。
bool writeFileBytes(const std::wstring& path, const uint8_t* data, std::size_t len);

// 整读文件；超过 maxBytes 视为失败（用来挡住明显损坏/不该有的大小）。
bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out, std::size_t maxBytes);

// 递归删除目录树。调用方负责先确认这个目录确实归本程序所有。
bool removeTree(const std::wstring& path);

// 同上，但保留 keepFile：安装目录里跑着的卸载器删不掉自己的镜像。
// keepFile 为空时行为等同 removeTree。
bool removeTreeExcept(const std::wstring& path, const std::wstring& keepFile);

bool deleteFile(const std::wstring& path);

// 改名（同一卷内，运行中的 exe 也能改名）。卸载器用它把自己变成一个唯一名字，
// 之后延迟删除才不会误伤可能刚装上的同名新文件。
bool moveFile(const std::wstring& from, const std::wstring& to);

// 借系统 cmd 在几秒后删掉运行中的卸载器与空目录：进程自己删不掉自己的镜像，
// 而 cmd 是一次性帮手，不需要活下来，也不会像临时副本那样把残骸留在 %TEMP%。
void scheduleDeleteAfterExit(const std::wstring& file, const std::wstring& dir);

// 挂到父控制台并把输出码页设成 UTF-8；控制台已经存在（重定向）时同样安全。
void attachConsoleUtf8();

// 按 UTF-8 字节写一行宽字符串。直接用 fwprintf 会在默认 C locale 下把中文写成乱码。
void consolePrint(const std::wstring& text, bool toError = false);

// IShellLinkW + IPersistFile 生成 .lnk。需要调用方已 CoInitializeEx。
bool createShortcut(const std::wstring& linkPath, const std::wstring& target, const std::wstring& workingDir,
                    const std::wstring& iconLocation, const std::wstring& description);

} // namespace fxwin
