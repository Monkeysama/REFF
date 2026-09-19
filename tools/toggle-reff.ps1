param(
    [ValidateSet('Disable', 'Enable', 'Status')][string]$Mode = 'Status',
    [string]$ReframeworkRoot = 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework'
)
$ErrorActionPreference = 'Stop'

# 性能基线开关处理 REFF 核心入口及已安装的可选示例入口，保留原文件并拒绝游戏运行时改名。
$targetRoot = [IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')
if ((Split-Path $targetRoot -Leaf) -ne 'reframework' -or -not (Test-Path -LiteralPath $targetRoot -PathType Container)) { throw '目标必须是现有 reframework 目录' }
$gameRoot = Split-Path $targetRoot -Parent
$gameProcess = if (Test-Path -LiteralPath (Join-Path $gameRoot 'MonsterHunterRise.exe')) { 'MonsterHunterRise' }
    elseif (Test-Path -LiteralPath (Join-Path $gameRoot 'MonsterHunterWilds.exe')) { 'MonsterHunterWilds' }
    else { throw '无法识别目标游戏' }
if (Get-Process -Name $gameProcess, reff-host -ErrorAction SilentlyContinue) { throw "请先手动退出 $gameProcess 和 REFF 宿主，再切换基线。" }

$entries = @(
    @{ path = 'plugins\REFF.dll'; disabled = 'plugins\REFF.dll.reff-disabled' },
    @{ path = 'autorun\REFF.lua'; disabled = 'autorun\REFF.lua.reff-disabled' }
)
$exampleEntry = @{ path = 'autorun\REFF.examples.lua'; disabled = 'autorun\REFF.examples.lua.reff-disabled' }
if ((Test-Path -LiteralPath (Join-Path $targetRoot $exampleEntry.path)) -or
    (Test-Path -LiteralPath (Join-Path $targetRoot $exampleEntry.disabled))) { $entries += $exampleEntry }
foreach ($entry in $entries) {
    $active = Join-Path $targetRoot $entry.path
    $disabled = Join-Path $targetRoot $entry.disabled
    if ($Mode -eq 'Disable') {
        if (-not (Test-Path -LiteralPath $active -PathType Leaf)) { throw "缺少 REFF 文件：$active" }
        if (Test-Path -LiteralPath $disabled) { throw "已存在停用文件：$disabled" }
        Move-Item -LiteralPath $active -Destination $disabled
    } elseif ($Mode -eq 'Enable') {
        if (-not (Test-Path -LiteralPath $disabled -PathType Leaf)) { throw "缺少停用文件：$disabled" }
        if (Test-Path -LiteralPath $active) { throw "目标文件已存在：$active" }
        Move-Item -LiteralPath $disabled -Destination $active
    }
}
$activeCount = @($entries | Where-Object { Test-Path -LiteralPath (Join-Path $targetRoot $_.path) }).Count
$disabledCount = @($entries | Where-Object { Test-Path -LiteralPath (Join-Path $targetRoot $_.disabled) }).Count
if ($activeCount -eq $entries.Count -and $disabledCount -eq 0) { Write-Output 'REFF 基线状态：启用' }
elseif ($activeCount -eq 0 -and $disabledCount -eq $entries.Count) { Write-Output 'REFF 基线状态：停用（无 REFF）' }
else { throw "REFF 加载入口状态不一致：启用 $activeCount，停用 $disabledCount" }
