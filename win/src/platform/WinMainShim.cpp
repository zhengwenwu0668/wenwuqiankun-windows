// Qt's win64_mingw packages are built against msvcrt.dll, but the toolchain used
// here is the ucrt variant of MinGW, whose import libraries have no legacy
// __imp___argc for Qt6EntryPoint.a to bind to. CMake therefore sets the
// qt_no_entrypoint property and this file provides the GUI-subsystem startup.

#if defined(_WIN32)

#include <windows.h>

#include <string>
#include <vector>

int main(int argc, char **argv);

namespace {

std::vector<std::string> utf8Arguments() {
    int argc = 0;
    LPWSTR *wide = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::string> out;
    if (!wide) return out;
    out.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text;
        if (needed > 1) {
            text.resize(static_cast<size_t>(needed - 1));
            ::WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), needed, nullptr, nullptr);
        }
        out.push_back(std::move(text));
    }
    ::LocalFree(wide);
    return out;
}

int dispatchToMain() {
    std::vector<std::string> args = utf8Arguments();
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const int argc = static_cast<int>(args.size());
    return argc > 0 ? main(argc, argv.data()) : main(0, argv.data());
}

}  // namespace

extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return dispatchToMain(); }
extern "C" int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) { return dispatchToMain(); }

#endif  // _WIN32
