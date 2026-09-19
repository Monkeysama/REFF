param(
    [switch]$SkipTests,
    [ValidateSet('Default', 'GameTest')][string]$Profile = 'Default',
    [switch]$ExperimentalGames
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# 构建配置使用独立目录和显式功能开关，避免默认构建覆盖已验收的游戏测试 DLL。
if ($ExperimentalGames -and $Profile -ne 'GameTest') { throw '实验游戏构建必须使用 -Profile GameTest。' }
$buildDirectoryName = if ($ExperimentalGames) { 'build-compat' } elseif ($Profile -eq 'GameTest') { 'build-ime' } else { 'build' }
$buildRoot = Join-Path $repoRoot $buildDirectoryName
$imeProxy = if ($Profile -eq 'GameTest') { 'ON' } else { 'OFF' }
$imeDiagnostics = if ($Profile -eq 'GameTest') { 'ON' } else { 'OFF' }
$perfDiagnostics = if ($Profile -eq 'GameTest') { 'ON' } else { 'OFF' }
$experimentalGameSupport = if ($ExperimentalGames) { 'ON' } else { 'OFF' }

# 优先使用已安装 VS 的 CMake，避免要求用户修改系统 PATH。
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw '未找到带 C++ 工具链的 Visual Studio' }
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'

& $cmake -S $repoRoot -B $buildRoot -G 'Visual Studio 18 2026' -A x64 `
    "-DREFF_ENABLE_IME_PROXY=$imeProxy" "-DREFF_ENABLE_IME_DIAGNOSTICS=$imeDiagnostics" `
    "-DREFF_ENABLE_PERF_DIAGNOSTICS=$perfDiagnostics" `
    "-DREFF_ENABLE_EXPERIMENTAL_GAMES=$experimentalGameSupport"
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }
& $cmake --build $buildRoot --config Release --parallel 6
if ($LASTEXITCODE -ne 0) { throw '原生构建失败' }
if (-not $SkipTests) {
    & $ctest --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw '自动测试未通过' }
}
$flavor = if ($ExperimentalGames) { "$Profile + 实验游戏" } else { $Profile }
Write-Host "REFF 原生构建完成：$flavor（$buildRoot）"
