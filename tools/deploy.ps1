param([string]$ReframeworkRoot='C:\Steam\steamapps\common\MonsterHunterWilds\reframework')
$ErrorActionPreference='Stop'
# 开发部署复用随包事务安装器；同样检查游戏退出、源哈希、备份和安装后哈希。
$repoRoot=Split-Path -Parent $PSScriptRoot
$source=Join-Path $repoRoot 'staging/reframework'
$installer=Join-Path $source 'reff/tools/install-reff.ps1'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw '请先运行 tools/stage.ps1 生成 WebView2 分发目录' }
& $installer -ReframeworkRoot $ReframeworkRoot -SourceRoot $source
