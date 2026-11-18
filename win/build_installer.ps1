# 产出单个可分发的 setup.exe：纯 Win32 安装向导，载荷内嵌，目标机不需要任何运行库。
#
#   powershell -ExecutionPolicy Bypass -File win\build_installer.ps1
#
# 顺序：Release 构建 → ctest 门禁 → windeployqt 组 staging → 放 uninstall.exe →
#       fx_pkgtool 打 FXPK 载荷 → fx_setup 把载荷编成 RT_RCDATA 内嵌。
# fx_setup 依赖上一步产出的 .fxpkg，所以它不在默认目标里，只在这条链上构建。
# 构建目录 win\build-installer 与产物目录 win\dist\installer 都可整目录删除重生成。
[CmdletBinding()]
param(
    [string]$QtDir = 'D:\twdl\toolchain\Qt\6.8.3\mingw_64',
    [string]$ToolchainRoot = 'D:\twdl\toolchain\Tools',
    [string]$BuildType = 'Release',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 下，外部命令非零退出不会触发 Stop；打开这个开关（6+ 已默认开启）
# 才能真做到"任一步失败就停"，而不是带着半成品载荷继续往下打。
if ($PSVersionTable.PSVersion.Major -lt 6) { $PSNativeCommandUseErrorActionPreference = $true }

$win = Split-Path -Parent $PSCommandPath
$build = Join-Path $win 'build-installer'
$stage = Join-Path $build 'stage\wenwuqiankun'
$outDir = Join-Path $win 'dist\installer'
$pkg = Join-Path $build 'wenwuqiankun.fxpkg'

# 必须用 msvcrt 版 MinGW（与 Qt win64_mingw 同一套 CRT），见 CMakeLists 顶部注释。
$mingw = Join-Path $ToolchainRoot 'mingw1310_64'
$env:PATH = ((@(
    (Join-Path $mingw 'bin'),
    (Join-Path $ToolchainRoot 'Ninja'),
    (Join-Path $ToolchainRoot 'CMake_64\bin'),
    (Join-Path $QtDir 'bin')
) -join ';') + ';' + $env:PATH)

function Invoke-Step {
    param([string]$Desc, [scriptblock]$Body)
    Write-Host "==> $Desc"
    & $Body
    if ($LASTEXITCODE -ne 0) { throw "$Desc 失败（exit $LASTEXITCODE）" }
}

Invoke-Step 'configure' {
    # 整个 -D 参数必须加引号：PowerShell 不展开以 - 开头的参数令牌，
    # 不加引号时 "$mingw" 会被原样送给 CMake（编译器路径变成字面量 $mingw/...）。
    cmake -S $win -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$BuildType" `
          "-DCMAKE_CXX_COMPILER=$mingw/bin/g++.exe" `
          "-DCMAKE_PREFIX_PATH=$QtDir"
}

# 构建默认目标（含 selfcheck 与 installcore_test，ctest 要用它们）。
Invoke-Step 'build app + tests' {
    cmake --build $build
}

# EXCLUDE_FROM_ALL 的目标不在默认目标里，必须点名构建，否则下面搬运的是上一次的旧镜像。
# 这一步曾经漏过：安装端用的是新逻辑，内嵌的 uninstall.exe 却还是旧版，卸载留下了残骸。
Invoke-Step 'build uninstaller + packer' {
    cmake --build $build --target fx_uninstaller fx_pkgtool
}

if (-not $SkipTests) {
    # 两道门禁：排盘数组逐项比对 + 安装器原语（SHA-256/DEFLATE/容器）对拍与模糊测试。
    Invoke-Step 'ctest' { ctest --test-dir $build --output-on-failure }
}

Invoke-Step 'stage payload' {
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item (Join-Path $build 'wenwuqiankun.exe') $stage
    windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
                (Join-Path $stage 'wenwuqiankun.exe') | Out-Null
    # 卸载器装在安装目录里，注册表的 UninstallString 指它。
    Copy-Item (Join-Path $build 'uninstall.exe') (Join-Path $stage 'uninstall.exe')
}

Invoke-Step 'pack and verify payload' {
    & (Join-Path $build 'fx_pkgtool.exe') $pkg $stage
    & (Join-Path $build 'fx_pkgtool.exe') --verify $pkg
}

Invoke-Step 'embed payload into setup.exe' {
    cmake --build $build --target fx_setup
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
# 名字里的版本号取自 CMakeLists 的 project(VERSION ...)，这里只做搬运，不再写一遍。
$setup = Get-ChildItem -Path $build -Filter 'wenwuqiankun-setup-*.exe' | Select-Object -First 1
if (-not $setup) { throw "没找到 fx_setup 产物" }
Copy-Item $setup.FullName $outDir -Force
$dst = Join-Path $outDir $setup.Name

Write-Host ''
Write-Host ("安装包：{0}  {1:N2} MB" -f $dst, ((Get-Item $dst).Length / 1MB))
Write-Host ("SHA-256：{0}" -f (Get-FileHash -Algorithm SHA256 $dst).Hash)
