param(
    [Parameter(Mandatory = $true)][ValidateSet('example.vue', 'example.react', 'example.html')][string]$PluginId,
    [string]$GameReframeworkRoot = 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework'
)

$ErrorActionPreference = 'Stop'

# 开发同步脚本只负责构建网页并复制插件资源，不启动或控制游戏；页面刷新由开发者手动完成。
$devRoot = Split-Path -Parent $PSScriptRoot
$pluginRoot = Join-Path $devRoot "web\plugins\$PluginId"
$shellRoot = Join-Path $devRoot 'web\shell'
$targetRoot = [IO.Path]::GetFullPath($GameReframeworkRoot).TrimEnd('\')
$targetPluginRoot = Join-Path $targetRoot "reff\plugins\$PluginId"

if (-not (Test-Path -LiteralPath $pluginRoot -PathType Container)) { throw "示例插件目录不存在：$pluginRoot" }
if (-not (Test-Path -LiteralPath (Join-Path $pluginRoot 'vite.config.ts') -PathType Leaf)) { throw "缺少 Vite 配置：$pluginRoot\vite.config.ts" }
if (-not (Test-Path -LiteralPath $shellRoot -PathType Container)) { throw "缺少 Web 依赖目录：$shellRoot" }
if (-not (Test-Path -LiteralPath $targetRoot -PathType Container)) { throw "游戏 REFramework 目录不存在：$targetRoot" }

$pnpm = Get-Command pnpm -ErrorAction SilentlyContinue
if (-not $pnpm) { throw '未找到 pnpm，请先安装 Node.js 与 pnpm。' }

# 先在示例开发工作区构建指定插件，其他插件不会被修改。
Push-Location $shellRoot
try {
    & $pnpm.Source exec vite build --config (Join-Path $pluginRoot 'vite.config.ts')
    if ($LASTEXITCODE -ne 0) { throw "Vite 构建失败：$PluginId" }
} finally { Pop-Location }

$manifest = Join-Path $pluginRoot 'manifest.json'
$dist = Join-Path $pluginRoot 'ui\dist'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "缺少插件 manifest：$manifest" }
if (-not (Test-Path -LiteralPath $dist -PathType Container)) { throw "构建未生成页面目录：$dist" }

# 只覆盖目标插件目录；不会触碰 REFF.dll、CEF、Shell 或其他插件。
New-Item -ItemType Directory -Force -Path $targetPluginRoot | Out-Null
Copy-Item -LiteralPath $manifest -Destination (Join-Path $targetPluginRoot 'manifest.json') -Force
Copy-Item -LiteralPath $dist -Destination (Join-Path $targetPluginRoot 'ui') -Recurse -Force

Write-Host "已同步 $PluginId 到 $targetPluginRoot"
Write-Host '请在游戏内重新打开该插件页面（切换到其他插件后再切回，或关闭并按 F8 重新打开 REFF）以加载最新资源。'
