param(
    [string]$ReframeworkRoot = 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'game-catalog.ps1')

# 诊断包只收集 REFF 自有兼容日志和不可逆哈希，不包含主日志、配置、缓存或用户输入内容。
$targetRoot = [IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')
if ((Split-Path $targetRoot -Leaf) -ne 'reframework') { throw '目标必须是游戏目录下的 reframework 目录。' }
$gameRoot = Split-Path $targetRoot -Parent
$game = Find-REFFGame $repoRoot $gameRoot @('verified', 'experimental')
if (-not $game) { throw '无法从游戏目录识别 REFF 游戏目标。' }
$logPath = Join-Path $targetRoot 'data\REFF\log\compatibility.log'
if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) { throw '尚未生成 compatibility.log，请先启动游戏并让 REFF 完成初始化。' }

if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'artifacts\compatibility' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$workRoot = Join-Path $OutputDirectory "REFF-$($game.target)-$stamp"
$archive = "$workRoot.zip"
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
Copy-Item -LiteralPath $logPath -Destination (Join-Path $workRoot 'compatibility.log')

$dllPath = Join-Path $targetRoot 'plugins\REFF.dll'
$report = [ordered]@{
    schemaVersion = 1
    target = $game.target
    game = $game.name
    supportStatus = $game.status
    collectedUtc = [DateTime]::UtcNow.ToString('o')
    reffDllSha256 = if (Test-Path -LiteralPath $dllPath -PathType Leaf) { (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
    operatingSystem = [Environment]::OSVersion.VersionString
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workRoot 'report.json') -Encoding utf8
Compress-Archive -LiteralPath $workRoot -DestinationPath $archive -CompressionLevel Optimal
Remove-Item -LiteralPath $workRoot -Recurse -Force
Write-Output "兼容诊断包已生成：$archive"
