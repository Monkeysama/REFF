param(
    [Parameter(Mandatory = $true)][ValidateSet('example.vue', 'example.react', 'example.html')][string]$PluginId,
    [Parameter(Mandatory = $true)][string]$GameReframeworkRoot
)

$ErrorActionPreference = 'Stop'

# 开发同步脚本只负责构建网页并复制插件资源，不启动或控制游戏；页面刷新由开发者手动完成。
$devRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
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
$targetDist = [IO.Path]::GetFullPath((Join-Path $targetPluginRoot 'ui\dist'))
if (-not $targetDist.StartsWith([IO.Path]::GetFullPath($targetPluginRoot).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '插件构建目标路径越界' }
# Vite 文件名包含内容哈希；先清理本插件旧 dist，避免开发同步长期累积不再引用的资源。
if (Test-Path -LiteralPath $targetDist) { Remove-Item -LiteralPath $targetDist -Recurse -Force }
Copy-Item -LiteralPath $dist -Destination (Join-Path $targetPluginRoot 'ui') -Recurse -Force
# 开发版本标记供 ui.dev.version 轮询；正式发布脚本不会复制此文件。
[DateTime]::UtcNow.ToString('o') | Set-Content -LiteralPath (Join-Path $targetPluginRoot '.reff-dev-version') -Encoding utf8

Write-Host "已同步 $PluginId 到 $targetPluginRoot"
Write-Host '开发示例会通过 installDevReload 自动检测标记并刷新当前页面；未安装该辅助函数时再手动切换插件页面。'
