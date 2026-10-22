param([string]$BuildRoot='',[string]$Archive='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot=Join-Path $repoRoot 'build-webview2-release' }
$fixture=Join-Path $repoRoot ('artifacts/webview2-p4/release-contract-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
# 仅验证本地发布契约，不调用 gh 或游戏；错误标签必须在构建或写文件前拒绝。
function Assert-Rejected([scriptblock]$Action,[string]$Reason) {
    $rejected=$false;try { & $Action | Out-Null } catch { $rejected=$true }
    if (-not $rejected) { throw "发布契约未拒绝：$Reason" }
}
$release=Join-Path $PSScriptRoot 'release.ps1'
$version=[string](Get-Content -LiteralPath (Join-Path $repoRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
$nextMajor=([int]$version.Split('.')[0])+1
foreach ($tag in @("v$version-preview1","v$version-rc.1","v$nextMajor.0.0",$version,"v0$version","v$version/../../outside")) {
    Assert-Rejected { & $release -Tag $tag -SkipBuild } "invalid stable tag $tag"
}
if ($Archive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    & (Join-Path $PSScriptRoot 'verify-release.ps1') -Archives @($Archive) | Out-Null
    $broken=Join-Path $fixture 'REFF-corrupt.zip'
    Copy-Item -LiteralPath $Archive -Destination $broken
    $zip=[IO.Compression.ZipFile]::Open($broken,[IO.Compression.ZipArchiveMode]::Update)
    try {
        # Windows PowerShell Compress-Archive 可写入反斜杠；按归一化路径定位真实归档项。
        $original=@($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -eq 'reframework/reff/ui/index.html' })
        if ($original.Count -ne 1) { throw '篡改夹具未找到唯一 Shell 入口' }
        $original[0].Delete()
        $entry=$zip.CreateEntry('reframework/reff/ui/index.html')
        $writer=New-Object IO.StreamWriter($entry.Open());try { $writer.Write('tampered') } finally { $writer.Dispose() }
    } finally { $zip.Dispose() }
    Assert-Rejected { & (Join-Path $PSScriptRoot 'verify-release.ps1') -Archives @($broken) } 'changed zip content'
    foreach ($unsafe in @('reframework/../outside','reframework/reff/ui/./a','reframework/reff/ui/a:stream','reframework/reff/ui/%2e%2e/a','reframework/reff/ui/a.')) {
        $badZip=Join-Path $fixture ('bad-'+[Guid]::NewGuid().ToString('N')+'.zip')
        $zip=[IO.Compression.ZipFile]::Open($badZip,[IO.Compression.ZipArchiveMode]::Create)
        try { $null=$zip.CreateEntry($unsafe) } finally { $zip.Dispose() }
        Assert-Rejected { & (Join-Path $PSScriptRoot 'verify-release.ps1') -Archives @($badZip) } 'unsafe zip path'
    }
}
@{passed=$true;stableTagContract=$true;archiveChecked=[bool]$Archive;gameValidated=$false} |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $fixture 'summary.json')
