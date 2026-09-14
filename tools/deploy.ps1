param([string]$ReframeworkRoot = 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$stageRoot = Join-Path $repoRoot 'staging\reframework'
$targetRoot = [IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')

# 只向明确的 REFramework 目录部署清单文件；游戏运行时禁止替换加载中的 DLL。
if ((Split-Path $targetRoot -Leaf) -ne 'reframework' -or -not (Test-Path -LiteralPath $targetRoot -PathType Container)) { throw '目标必须是现有 reframework 目录' }
if (Get-Process -Name MonsterHunterWilds -ErrorAction SilentlyContinue) { throw '请先退出 Wilds，再部署原生插件。' }
$manifestJson = Get-Content -LiteralPath (Join-Path $repoRoot 'staging\manifest.json') -Raw | ConvertFrom-Json
# Windows PowerShell 5.1 会把 ConvertFrom-Json 的顶层数组作为单个管道对象返回；foreach 显式展开后再进入部署循环。
$manifest = @()
foreach ($entry in $manifestJson) { $manifest += $entry }
$previousPath = Join-Path $targetRoot 'reff\installed-manifest.json'
$previous = @{}
if (Test-Path -LiteralPath $previousPath) { foreach ($entry in (Get-Content -LiteralPath $previousPath -Raw | ConvertFrom-Json)) { $previous[$entry.path] = $entry.sha256 } }
$current = @{}
foreach ($entry in $manifest) { $current[$entry.path] = $entry.sha256 }

# 先完成所有路径与哈希检查，发现未知文件冲突时整次部署停止。
foreach ($entry in $manifest) {
    if ($entry.path -notmatch '^(plugins/REFF\.dll|autorun/REFF(\.examples)?\.lua|autorun/reff/(bridge|sdk)\.lua|reff/(runtime|ui|plugins|licenses)/.+|reff/dependencies\.lock\.json)$') { throw "非 REFF 路径：$($entry.path)" }
    $destination = [IO.Path]::GetFullPath((Join-Path $targetRoot $entry.path))
    if (-not $destination.StartsWith($targetRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '目标路径越界' }
    $ancestor = $destination
    while ($ancestor.Length -ge $targetRoot.Length) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "拒绝重解析路径：$ancestor" }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    $source = Join-Path $stageRoot $entry.path
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "staging 文件已改变：$($entry.path)" }
    if (Test-Path -LiteralPath $destination) {
        if (-not $previous.ContainsKey($entry.path)) { throw "发现不属于既有部署清单的文件：$destination" }
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $previous[$entry.path]) { throw "目标文件被外部修改：$destination" }
    }
}

# 旧清单中已移除的文件只能在哈希仍匹配时清理，避免删除用户或其他 Mod 修改过的内容。
$obsolete = @($previous.Keys | Where-Object { -not $current.ContainsKey($_) })
foreach ($path in $obsolete) {
    if ($path -notmatch '^(plugins/REFF\.dll|autorun/REFF(\.examples)?\.lua|autorun/reff/(bridge|sdk)\.lua|reff/(runtime|ui|plugins|licenses)/.+|reff/dependencies\.lock\.json)$') { throw "旧清单包含非 REFF 路径：$path" }
    $destination = [IO.Path]::GetFullPath((Join-Path $targetRoot $path))
    if (-not $destination.StartsWith($targetRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '旧清单目标路径越界' }
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $previous[$path]) { throw "旧 REFF 文件被外部修改：$destination" }
    }
}

# 保留本次覆盖文件，任何失败都可从本地备份恢复；不清空任何游戏目录。
$backupRoot = Join-Path $repoRoot ('artifacts\deploy-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
foreach ($entry in $manifest) {
    $destination = Join-Path $targetRoot $entry.path
    if (Test-Path -LiteralPath $destination) {
        $backup = Join-Path $backupRoot $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
        Copy-Item -LiteralPath $destination -Destination $backup
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $stageRoot $entry.path) -Destination $destination -Force
}
foreach ($path in $obsolete) {
    $destination = Join-Path $targetRoot $path
    if (Test-Path -LiteralPath $destination) {
        $backup = Join-Path $backupRoot $path
        New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
        Copy-Item -LiteralPath $destination -Destination $backup
        Remove-Item -LiteralPath $destination -Force
    }
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'staging\manifest.json') -Destination $previousPath -Force
& icacls (Join-Path $targetRoot 'reff\runtime') /grant '*S-1-15-2-2:(OI)(CI)(RX)' /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Runtime 沙箱读取权限设置失败' }
Write-Host "已部署 $($manifest.Count) 个 REFF 文件；未修改其他 Mod。"
