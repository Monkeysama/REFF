# 分发、安装、恢复共用边界；只管理 REFF 文件，不认领用户设置或第三方插件。
function Resolve-REFFFile([string]$Root,[string]$Relative) {
    if ($Relative -notmatch '^(plugins/REFF\.dll|autorun/REFF(\.examples)?\.lua|autorun/reff/(bridge|sdk)\.lua|reff/(runtime|ui|licenses|tools|compatibility)/.+|reff/(dependencies\.lock|package|package-manifest|installed-manifest)\.json|reff/plugins/example\.(vue|react|html)/.+)$' -or
        $Relative -match '[\\:%<>"|?*#]' -or $Relative -match '(^|/)(\.|\.\.|)(/|$)' -or $Relative -match '[. ](/|$)') { throw "非 REFF 文件路径：$Relative" }
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw '文件路径越界' }
    # 逐级检查到磁盘根，拒绝通过 junction 将安装或恢复导向别处。
    $ancestor=$full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "拒绝重解析路径：$ancestor" }
        }
        $ancestor=Split-Path -Parent $ancestor
    }
    return $full
}
function Read-REFFManifest([string]$File,[string]$Root,[switch]$Verify) {
    $entries=[IO.File]::ReadAllText($File,[Text.Encoding]::UTF8) | ConvertFrom-Json
    if (-not $entries) { throw '文件清单为空' }
    $seen=@{}
    foreach ($entry in $entries) {
        $path=Resolve-REFFFile $Root ([string]$entry.path)
        if ($seen.ContainsKey([string]$entry.path) -or $entry.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw '清单重复或哈希无效' }
        $seen[[string]$entry.path]=$true
        if ($Verify -and ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256)) { throw "文件哈希不符：$($entry.path)" }
    }
    return $entries
}
function Write-REFFManifest([string]$Root,[string]$File) {
    $entries=@(Get-ChildItem -LiteralPath $Root -File -Recurse | Where-Object { $_.FullName -ne $File } | Sort-Object FullName | ForEach-Object {
        $relative=$_.FullName.Substring($Root.TrimEnd('\').Length+1).Replace('\','/')
        $null=Resolve-REFFFile $Root $relative
        [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant();bytes=$_.Length}
    })
    ConvertTo-Json -InputObject @($entries) -Depth 6 | Set-Content -LiteralPath $File -Encoding utf8
    return $entries
}
# 递归删除只用于 staging 直属生成目录；先检查解析后的绝对路径及父级链接。
function Reset-REFFStage([string]$Repository,[string]$Directory) {
    $base=[IO.Path]::GetFullPath((Join-Path $Repository 'staging')).TrimEnd('\')
    $full=[IO.Path]::GetFullPath($Directory).TrimEnd('\')
    if (-not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $full -Parent) -ne $base) { throw "只允许重建 staging 直属目录：$full" }
    $null=Resolve-REFFFile $full 'reff/package.json'
    if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Recurse -Force }
    New-Item -ItemType Directory -Path $full -Force | Out-Null
}
