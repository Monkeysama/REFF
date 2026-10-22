param([switch]$SkipFetch,[switch]$SkipWeb,[switch]$IncludeGame)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot

# 独立构建只准备锁定 SDK；不安装系统 Runtime，不触碰游戏。
if(-not $SkipFetch){& (Join-Path $PSScriptRoot 'fetch-webview2-sdk.ps1')}
if(-not $SkipWeb){& (Join-Path $PSScriptRoot 'build-web.ps1') -IncludeExamples}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsRoot){throw '未找到 Visual Studio C++ 工具链'}
$cmake=Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$buildRoot=Join-Path $repoRoot 'build-webview2-p1'
& $cmake -S (Join-Path $repoRoot 'native\webview2_host') -B $buildRoot -G 'Visual Studio 18 2026' -A x64
if($LASTEXITCODE -ne 0){throw 'P1 宿主配置失败'}
& $cmake --build $buildRoot --config Release --parallel 6
if($LASTEXITCODE -ne 0){throw 'P1 宿主构建失败'}
& (Join-Path $PSScriptRoot 'test-webview2-p1.ps1') -BuildRoot $buildRoot
& (Join-Path $PSScriptRoot 'test-webview2-p1.ps1') -BuildRoot $buildRoot -Behavior

# 游戏侧只选择新宿主，保留原有 Lua/输入/DX12；隔离目录使用全部游戏目标。
if($IncludeGame){
    $gameBuild=Join-Path $repoRoot 'build-webview2-p1-game'
    & $cmake -S $repoRoot -B $gameBuild -G 'Visual Studio 18 2026' -A x64 `
        -DREFF_BUILD_WEBVIEW2_HOST=OFF -DREFF_ENABLE_WEBVIEW2=ON `
        -DREFF_ENABLE_IME_PROXY=ON -DREFF_ENABLE_IME_DIAGNOSTICS=ON `
        -DREFF_ENABLE_PERF_DIAGNOSTICS=ON
    if($LASTEXITCODE -ne 0){throw 'P1 游戏 DLL 配置失败'}
    & $cmake --build $gameBuild --config Release --parallel 6
    if($LASTEXITCODE -ne 0){throw 'P1 游戏 DLL 构建失败'}
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $gameBuild -C Release --output-on-failure
    if($LASTEXITCODE -ne 0){throw 'P1 游戏 DLL 合约测试失败'}
}
Get-FileHash -LiteralPath (Join-Path $buildRoot 'Release\reff-webview2-host.exe') -Algorithm SHA256
