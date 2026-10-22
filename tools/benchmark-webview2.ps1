param([string]$OutputDirectory = '', [switch]$IncludeGpu, [string]$BuildRoot = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot ('artifacts/webview2/perf-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $BuildRoot) { $BuildRoot = Join-Path $repoRoot 'build-webview2-release' }
$driver = Join-Path $BuildRoot 'Release/reff-wv2-benchmark.exe'
$sourceAssets = Join-Path $repoRoot 'tests/webview2/benchmark-assets'
$collector = Join-Path $PSScriptRoot 'observe-resources.ps1'

# 只运行正式 WebView2 独立宿主，不操作游戏；历史跨引擎数据保存在 artifacts 中。
$backend = 'webview2'
$hostFile = Join-Path $BuildRoot 'Release/reff-webview2-host.exe'
& {
    if (-not (Test-Path -LiteralPath $hostFile)) { throw "缺少宿主：$hostFile" }
    $output = Join-Path $OutputDirectory $backend
    if (Test-Path -LiteralPath $output) { throw '性能输出目录已存在，拒绝混入旧样本' }
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    # 页面复制到本轮目录，保证日志与源码隔离。
    $assets = Join-Path $output 'reff/ui'
    New-Item -ItemType Directory -Path $assets -Force | Out-Null
    foreach ($name in @('index.html','style.css','app.js')) {
        Copy-Item -LiteralPath (Join-Path $sourceAssets $name) -Destination (Join-Path $assets $name)
    }
    $runtime = Split-Path $hostFile -Parent
    # 使用 Windows PowerShell 验证实际发布环境的脚本兼容性，后台无终端窗口。
    $arguments = @('-NoProfile','-File',('"'+$collector+'"'),'-DurationSeconds','42','-IntervalSeconds','1',
        '-RuntimeDirectory',('"'+$runtime+'"'),'-OutputDirectory',('"'+$output+'"'))
    if ($IncludeGpu) { $arguments += '-IncludeGpu' }
    $observer = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $output 'collector.stdout.txt') -RedirectStandardError (Join-Path $output 'collector.stderr.txt')
    # PowerShell 5.1 重定向子进程退出后可能丢失退出码；提前持有句柄直到采样结束。
    $null = $observer.Handle
    & $driver $hostFile $assets $output
    $driverCode = $LASTEXITCODE
    if (-not $observer.WaitForExit(15000)) { throw '独立采样未在预计时间内结束；请查看采样进程输出' }
    if ($observer.ExitCode -ne 0) { throw "采样失败：$output/collector.stderr.txt" }
    if ($driverCode -ne 0) { throw "独立性能驱动失败：$backend" }
    & powershell.exe -NoProfile -File $collector -Mode Report -OutputDirectory $output
    if ($LASTEXITCODE -ne 0) { throw '性能汇总失败' }
    [pscustomobject]@{ backend = $backend; sha256 = (Get-FileHash -LiteralPath $hostFile).Hash; output = $output } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'build.json') -Encoding utf8
}
Write-Output "独立采样完成：$OutputDirectory；不是游戏内性能验收。"
