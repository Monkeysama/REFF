param([Parameter(Mandatory=$true)][string]$ReframeworkRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-files.ps1')
# 正式包可离线收集；仅输出白名单版本、数值与状态，不复制原始日志或输入文本。
$target=[IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')
if ((Split-Path $target -Leaf) -ne 'reframework') { throw '目标必须是游戏目录下的 reframework' }
$null=Resolve-REFFFile $target 'reff/package.json'
function Assert-DiagnosticPath([string]$Path) {
    $pathPart=[IO.Path]::GetFullPath($Path)
    if (-not $pathPart.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) { throw '诊断路径越界' }
    while ($pathPart) {
        if ((Test-Path -LiteralPath $pathPart) -and ((Get-Item -LiteralPath $pathPart -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw '诊断路径包含重解析点' }
        $pathPart=Split-Path -Parent $pathPart
    }
}
$metadata=Get-Content -LiteralPath (Resolve-REFFFile $target 'reff/package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$report=[ordered]@{schemaVersion=2;backend='webview2';collectedUtc=[DateTime]::UtcNow.ToString('o');operatingSystem=[Environment]::OSVersion.Version.ToString();gameValidated=$false}
if ([string]$metadata.version -match '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') { $report.packageVersion=[string]$metadata.version }
foreach ($pair in @(@('reffDllSha256','plugins/REFF.dll'),@('hostSha256','reff/runtime/reff-webview2-host.exe'))) {
    $file=Resolve-REFFFile $target $pair[1]
    if (Test-Path -LiteralPath $file -PathType Leaf) { $report[$pair[0]]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$summary=Join-Path $target 'data/REFF/log/webview2/summary.json'
Assert-DiagnosticPath $summary
if ((Test-Path -LiteralPath $summary -PathType Leaf) -and (Get-Item -LiteralPath $summary).Length -le 1MB) {
    try {
        $hostSummary=Get-Content -LiteralPath $summary -Raw -Encoding UTF8 | ConvertFrom-Json
        $safe=[ordered]@{}
        foreach ($key in @('runtime','minimumRuntime')) {
            $value=[string]$hostSummary.$key
            if ($value -match '^\d+\.\d+\.\d+\.\d+$') { $safe[$key]=$value }
        }
        foreach ($key in @('osBuild','minimumOsBuild','exitCode','hresult','runtimeDetectionHresult','publishedFrames','publishedBytes','firstPublishedMs','navigationReadyMs')) {
            $value=$hostSummary.$key
            if ($null -ne $value -and ($value -is [ValueType]) -and $value -isnot [bool]) { $safe[$key]=$value }
        }
        $report.host=$safe
    } catch { $report.hostSummaryUnreadable=$true }
}
$log=Join-Path $target 'data/REFF/log/compatibility.log'
Assert-DiagnosticPath $log
if ((Test-Path -LiteralPath $log -PathType Leaf) -and (Get-Item -LiteralPath $log).Length -le 1MB) {
    $text=[IO.File]::ReadAllText($log)
    $report.native=[ordered]@{initialized=$text.Contains('initialization=complete');hostStarted=$text.Contains('host process=started');handshake=$text.Contains('host handshake=ok');dx12=$text.Contains('renderer initialized=d3d12');rendererReset=$text.Contains('renderer reset')}
    $matched=[regex]::Match($text,'target=([A-Z0-9_]{1,48}) status=(tested|untested)')
    if ($matched.Success) { $report.native.target=$matched.Groups[1].Value;$report.native.status=$matched.Groups[2].Value }
}
$work=Join-Path $target ('data/REFF/diagnostics/REFF-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
Assert-DiagnosticPath (Join-Path $work 'report.json')
New-Item -ItemType Directory -Path $work -Force | Out-Null
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $work 'report.json') -Encoding UTF8
Compress-Archive -LiteralPath (Join-Path $work 'report.json') -DestinationPath "$work.zip" -CompressionLevel Optimal
Write-Output "诊断包已生成：$work.zip"
