param(
    [Parameter(Mandatory = $true)][string]$ReframeworkRoot
)
$ErrorActionPreference = 'Stop'

# 安装包诊断收集器只读取 REFF 自有低频日志；不依赖源码仓库，也不收集用户设置或输入内容。
$targetRoot = [IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')
if ((Split-Path $targetRoot -Leaf) -ne 'reframework') { throw '目标必须是游戏目录下的 reframework 目录。' }
$metadataPath = Join-Path $targetRoot 'reff\compatibility\package.json'
$logPath = Join-Path $targetRoot 'data\REFF\log\compatibility.log'
$dllPath = Join-Path $targetRoot 'plugins\REFF.dll'
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) { throw '当前安装不是带诊断元数据的 REFF 实验兼容包。' }
if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) { throw '尚未生成 compatibility.log，请先启动游戏并让 REFF 完成初始化。' }
if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) { throw '没有找到 reframework\plugins\REFF.dll。' }

$metadata = [IO.File]::ReadAllText($metadataPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
$targets = if ($metadata.targets) { @($metadata.targets | ForEach-Object { [string]$_ }) } else { @([string]$metadata.target) }
$outputRoot = Join-Path $targetRoot 'data\REFF\diagnostics'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$workRoot = Join-Path $outputRoot "REFF-$($metadata.target)-$stamp"
$archive = "$workRoot.zip"
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
Copy-Item -LiteralPath $logPath -Destination (Join-Path $workRoot 'compatibility.log')
[ordered]@{
    schemaVersion = 1
    target = ($targets -join ',')
    targets = $targets
    games = @($metadata.games)
    supportStatus = [string]$metadata.supportStatus
    packageVersion = [string]$metadata.version
    collectedUtc = [DateTime]::UtcNow.ToString('o')
    reffDllSha256 = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant()
    operatingSystem = [Environment]::OSVersion.VersionString
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workRoot 'report.json') -Encoding utf8
Compress-Archive -LiteralPath $workRoot -DestinationPath $archive -CompressionLevel Optimal
Remove-Item -LiteralPath $workRoot -Recurse -Force
Write-Output "兼容诊断包已生成：$archive"
