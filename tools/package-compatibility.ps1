param(
    [string]$Target = '',
    [switch]$AllExperimental,
    [switch]$SkipBuild,
    [string]$PackageVersion = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'game-catalog.ps1')

# 实验兼容包与正式 Release 完全隔离，只允许目录中明确标记为 experimental 的目标；默认一次覆盖全部实验目标。
$normalizedTarget = $Target.Trim().ToUpperInvariant()
$catalog = Get-REFFGameCatalog $repoRoot
$selectedGames = if ($AllExperimental -or -not $normalizedTarget) {
    @($catalog.games | Where-Object { [string]$_.status -eq 'experimental' })
} else {
    @($catalog.games | Where-Object { ([string]$_.target).ToUpperInvariant() -eq $normalizedTarget })
}
if (-not $selectedGames) { throw "没有找到可打包的 experimental 目标：$normalizedTarget" }
if (@($selectedGames | Where-Object { [string]$_.status -ne 'experimental' })) { throw '统一实验包只能包含 experimental 目标。' }
$selectedTargets = @($selectedGames | ForEach-Object { [string]$_.target })
$packageKey = if ($selectedTargets.Count -gt 1) { 'modern' } else { $selectedTargets[0] }

$version = if ($PackageVersion) { $PackageVersion.TrimStart('v') } else { [string](([IO.File]::ReadAllText((Join-Path $repoRoot 'version.json'), [Text.Encoding]::UTF8) | ConvertFrom-Json).version) }
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') { throw "实验包版本号无效：$version" }
$buildRoot = Join-Path $repoRoot 'build-compat'
$compatibilityStageRoot = Join-Path $repoRoot "staging\compatibility-build-$packageKey"
$stageRoot = Join-Path $compatibilityStageRoot 'reframework'
$profilePath = Join-Path $compatibilityStageRoot 'build-profile.json'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'artifacts\compatibility' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

# 构建路径固定启用透明 IME 代理和实验游戏开关，避免误用正式 DLL 或默认构建产物。
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-web.ps1') -IncludeExamples
    if ($LASTEXITCODE -ne 0) { throw '示例 Web 资源构建失败。' }
    & (Join-Path $PSScriptRoot 'build.ps1') -Profile GameTest -ExperimentalGames
    if ($LASTEXITCODE -ne 0) { throw '实验原生构建失败。' }
    & (Join-Path $PSScriptRoot 'stage.ps1') -BuildRoot $buildRoot -IncludeExamples -StagingRoot $compatibilityStageRoot
    if ($LASTEXITCODE -ne 0) { throw '实验 staging 生成失败。' }
}

if (-not (Test-Path -LiteralPath $profilePath -PathType Leaf)) { throw '缺少 staging/build-profile.json；请先生成实验 staging。' }
$profile = [IO.File]::ReadAllText($profilePath, [Text.Encoding]::UTF8) | ConvertFrom-Json
if (-not [bool]$profile.experimentalGames) { throw '当前 staging 不是实验游戏构建，拒绝生成兼容包。' }
if (-not (Test-Path -LiteralPath (Join-Path $stageRoot 'plugins\REFF.dll') -PathType Leaf)) { throw '实验 staging 缺少 REFF.dll。' }

# 临时目录只复制 staging 清单内的 Runtime，再加入目标元数据、测试指南和无仓库依赖的收集器。
$workParent = Join-Path $repoRoot "staging\compatibility-package-$packageKey"
$packageRoot = Join-Path $workParent 'reframework'
if (Test-Path -LiteralPath $workParent) { Remove-Item -LiteralPath $workParent -Recurse -Force }
New-Item -ItemType Directory -Force -Path $workParent | Out-Null
Copy-Item -LiteralPath $stageRoot -Destination $workParent -Recurse -Force
$compatibilityRoot = Join-Path $packageRoot 'reff\compatibility'
$toolsRoot = Join-Path $packageRoot 'reff\tools'
New-Item -ItemType Directory -Force -Path $compatibilityRoot, $toolsRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\compatibility-testing.zh-CN.md') -Destination (Join-Path $compatibilityRoot 'README.zh-CN.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'collect-installed-compatibility.ps1') -Destination $toolsRoot
[ordered]@{
    schemaVersion = 1
    version = $version
    targets = $selectedTargets
    games = @($selectedGames | ForEach-Object { [string]$_.name })
    supportStatus = 'experimental'
    target = if ($selectedTargets.Count -eq 1) { $selectedTargets[0] } else { $null }
    game = if ($selectedGames.Count -eq 1) { [string]$selectedGames[0].name } else { $null }
    renderer = (@($selectedGames | ForEach-Object { [string]$_.renderer } | Select-Object -Unique) -join ',')
    experimentalGames = $true
    createdUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $compatibilityRoot 'package.json') -Encoding utf8

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$archive = Join-Path $OutputDirectory "REFF-$version-$packageKey-experimental.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive -CompressionLevel Optimal

# 归档校验确保实验包保持 reframework 顶层结构并包含可操作的诊断入口。
$entries = @(& tar -tf $archive 2>$null | ForEach-Object { ([string]$_).TrimStart('./').Replace('\', '/') })
if ($LASTEXITCODE -ne 0 -or $entries.Count -eq 0) { throw "无法读取实验包：$archive" }
foreach ($entry in $entries) {
    if ($entry -and -not $entry.StartsWith('reframework/')) { throw "实验包存在错误的顶层路径：$entry" }
}
foreach ($required in @(
    'reframework/plugins/REFF.dll',
    'reframework/autorun/REFF.examples.lua',
    'reframework/reff/compatibility/README.zh-CN.md',
    'reframework/reff/compatibility/package.json',
    'reframework/reff/tools/collect-installed-compatibility.ps1'
)) {
    if ($required -notin $entries) { throw "实验包缺少关键文件：$required" }
}
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Remove-Item -LiteralPath $workParent -Recurse -Force
Write-Output "REFF 实验兼容包已生成：$archive"
Write-Output "SHA-256：$hash"
