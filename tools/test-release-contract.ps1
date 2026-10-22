param([string]$BuildRoot='',[string]$Archive='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot=Join-Path $repoRoot 'build-webview2-release' }
$fixture=Join-Path $repoRoot ('artifacts/webview2-p4/release-contract-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
# 只测试发布判断函数，不调用 gh。合成通过记录位于独立夹具，不能充当真实游戏验收。
$gate=Join-Path $PSScriptRoot 'verify-game-validation.ps1'
function Assert-Rejected([scriptblock]$Action,[string]$Reason) {
    $rejected=$false;try { & $Action | Out-Null } catch { $rejected=$true }
    if (-not $rejected) { throw "发布契约未拒绝：$Reason" }
}
$checks=@();foreach ($required in @(@('MHWILDS','P2'),@('MHWILDS','P3'),@('MHRISE','P3'),@('RE4','P3'))) {
    $checks+=@{game=$required[0];scope=$required[1];status='passed';evidence='synthetic-fixture-only'}
}
$validation=@{schemaVersion=1;backend='webview2';dllSha256=(Get-FileHash -LiteralPath (Join-Path $BuildRoot 'Release/REFF.dll')).Hash;hostSha256=(Get-FileHash -LiteralPath (Join-Path $BuildRoot 'Release/reff-webview2-host.exe')).Hash;checks=$checks}
$file=Join-Path $fixture 'synthetic-validation.json'
# 失败用例独立构造，不依赖真实验收配置始终处于 pending。
$checks[0].status='pending'
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
Assert-Rejected { & $gate -ValidationFile $file -BuildRoot $BuildRoot } 'pending manual acceptance'
$checks[0].status='passed'
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
& $gate -ValidationFile $file -BuildRoot $BuildRoot | Out-Null
$validation.hostSha256=('0'*64)
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
Assert-Rejected { & $gate -ValidationFile $file -BuildRoot $BuildRoot } 'different native build'
# 每个拒绝分支仅破坏自身条件，避免上一用例的错误哈希掩盖缺失游戏记录。
$validation.hostSha256=(Get-FileHash -LiteralPath (Join-Path $BuildRoot 'Release/reff-webview2-host.exe')).Hash
$validation.checks=$checks[0..2]
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
Assert-Rejected { & $gate -ValidationFile $file -BuildRoot $BuildRoot } 'missing game'
$validation.checks=$checks
$checks[3].status='deferred'
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
Assert-Rejected { & $gate -ValidationFile $file -BuildRoot $BuildRoot } 'unapproved deferral'
$validation.scopeAdjustment=@{approvedBy='user';reason='synthetic fixture only';evidence='synthetic-fixture-only'}
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
& $gate -ValidationFile $file -BuildRoot $BuildRoot | Out-Null
$validation.scopeAdjustment.reason=''
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
Assert-Rejected { & $gate -ValidationFile $file -BuildRoot $BuildRoot } 'missing deferral reason'
$validation.scopeAdjustment.reason='synthetic fixture only'
$checks[2].status='deferred'
$validation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
Assert-Rejected { & $gate -ValidationFile $file -BuildRoot $BuildRoot } 'core game deferral'
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
@{passed=$true;pendingRejected=$true;nativeHashBinding=$true;missingGameRejected=$true;explicitRe4DeferralPassed=$true;unapprovedDeferralRejected=$true;coreGameDeferralRejected=$true;archiveChecked=[bool]$Archive;gameValidated=$false} |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $fixture 'summary.json')
