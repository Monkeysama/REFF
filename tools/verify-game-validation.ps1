param(
    [Parameter(Mandatory=$true)][string]$ValidationFile,
    [Parameter(Mandatory=$true)][string]$BuildRoot
)
$ErrorActionPreference='Stop'
# 只读发布门槛：核心游戏必须通过；仅 RE4 可依据用户明确的范围调整延期，仍记录为未测。
$validation=Get-Content -LiteralPath $ValidationFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($validation.schemaVersion -ne 1 -or $validation.backend -ne 'webview2') { throw '发布验收记录版本或后端不匹配' }
foreach ($required in @(@('MHWILDS','P2'),@('MHWILDS','P3'),@('MHRISE','P3'),@('RE4','P3'))) {
    $checks=@($validation.checks | Where-Object { $_.game -eq $required[0] -and $_.scope -eq $required[1] })
    if ($required[0] -eq 'RE4' -and $checks.Count -eq 1 -and $checks[0].status -eq 'deferred') {
        $adjustment=$validation.scopeAdjustment
        if ($adjustment.approvedBy -ne 'user' -or [string]::IsNullOrWhiteSpace($adjustment.reason) -or
            [string]::IsNullOrWhiteSpace($adjustment.evidence) -or $checks[0].evidence -ne $adjustment.evidence) {
            throw 'RE4 延期缺少用户范围调整记录；未测不能自动视为通过。'
        }
        Write-Output 'RE4 本轮未测，按用户明确调整的范围延期。'
        continue
    }
    if ($checks.Count -ne 1 -or $checks[0].status -ne 'passed' -or [string]::IsNullOrWhiteSpace($checks[0].evidence)) {
        throw "游戏人工验收尚未完成：$($required -join '/')；本地产物仍可生成，暂停公开发布。"
    }
}
foreach ($pair in @(@('dllSha256','Release/REFF.dll'),@('hostSha256','Release/reff-webview2-host.exe'))) {
    $recorded=[string]$validation.($pair[0])
    if ($recorded -notmatch '^[a-fA-F0-9]{64}$' -or (Get-FileHash -LiteralPath (Join-Path $BuildRoot $pair[1]) -Algorithm SHA256).Hash -ne $recorded) {
        throw '人工验收对应的原生产物与当前发布构建不同，请验证当前构建后更新证据'
    }
}
Write-Output '当前 WebView2 原生产物已满足本轮约定范围的游戏人工验收。'
