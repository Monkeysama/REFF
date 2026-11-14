param([string]$BuildRoot = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot = Join-Path $repoRoot 'build-webview2-release' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot ('artifacts/webview2/cold-start-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$driverFile = Join-Path $BuildRoot 'Release/reff-wv2-cold-start.exe'
$hostFile = Join-Path $BuildRoot 'Release/reff-webview2-host.exe'
foreach ($file in @($driverFile, $hostFile)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "缺少构建产物：$file" }
}
if (Test-Path -LiteralPath $OutputDirectory) { throw '输出目录已存在，拒绝混入旧冷启动样本' }

# 仅运行驱动自己拥有的三个正式隐藏宿主；无 selftest/网页脚本注入，不启动或操作游戏。
# 驱动创建隔离目录、空缓存及 Job，失败也保存 JSON；本脚本不构建、不部署、不结束其他进程。
& $driverFile $hostFile $OutputDirectory
$driverCode = $LASTEXITCODE
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw "驱动未创建输出目录（exit=$driverCode）" }
[pscustomobject]@{
    hostSha256 = (Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash
    driverSha256 = (Get-FileHash -LiteralPath $driverFile -Algorithm SHA256).Hash
    gameValidated = $false
    selfTestEnabled = $false
    scriptInjected = $false
    driverExitCode = $driverCode
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'build.json') -Encoding UTF8
if ($driverCode -ne 0) { throw "冷启动独立回归失败（exit=$driverCode）：$OutputDirectory" }
$summary = Get-Content -LiteralPath (Join-Path $OutputDirectory 'cold-start-summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $summary.passed -or $summary.runs.Count -ne 3) { throw "回归摘要不完整：$OutputDirectory" }
Write-Host "三轮单次显示冷启动/隐藏缩放恢复通过：$OutputDirectory；不是游戏内验收。"
