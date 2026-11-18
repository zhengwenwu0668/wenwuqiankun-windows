#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fxinst {

struct Options {
    std::wstring targetDir;
    bool desktopShortcut = false;
};

using ProgressFn = std::function<void(std::size_t done, std::size_t total, const std::wstring& current)>;

bool defaultTargetDir(std::wstring& out);

// 用户在向导里选的是上级目录，真正的安装目录是它下面的 wenwuqiankun。
// 末级已经是 wenwuqiankun 的路径原样返回（大小写不敏感），这样注册表里的旧值、
// 以及用户手动补全的那一层，都不会被再拼一遍成 …\wenwuqiankun\wenwuqiankun。
std::wstring resolveTargetDir(const std::wstring& parent);

// 载荷优先取 exe 内嵌资源；内嵌缺失时回退到旁边那个 .fxpkg，方便改了脚本不必重装 exe 就能试装。
bool loadPackageImage(std::vector<uint8_t>& image, std::wstring& error);

bool install(const Options& opt, const ProgressFn& progress, std::wstring& error);

// 只删属于本程序的东西：安装目录里必须能看到主程序，否则拒绝执行。
// keepFile 是给安装目录里那个正在运行的卸载器用的——它删不掉自己的镜像，
// 由调用方在退出后延迟删除；不在 targetDir 下面的路径一律不认。
bool uninstall(const std::wstring& targetDir, const std::wstring& keepFile, std::wstring& error);

std::wstring startMenuLink();
std::wstring desktopLink();

} // namespace fxinst
