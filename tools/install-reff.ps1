param(
    [Parameter(Mandatory=$true)][string]$ReframeworkRoot,
    [string]$SourceRoot='',
    [string]$RestoreBackup=''
)
$ErrorActionPreference='Stop'
# 随包路径在参数绑定完成后解析，兼容 Windows PowerShell 5.1 的 -File 入口。
if (-not $SourceRoot) { $SourceRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
. (Join-Path $PSScriptRoot 'package-files.ps1')
# 随包离线安装；进程只读检查，不启动/退出游戏，不下载或安装系统 Runtime。
$target=[IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')
if ((Split-Path $target -Leaf) -ne 'reframework') { throw '目标必须是游戏的 reframework 目录' }
$null=Resolve-REFFFile $target 'reff/installed-manifest.json'
$gameRoot=Split-Path $target -Parent
$catalog=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'game-support.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$game=$null
foreach ($candidate in $catalog.games) {
    foreach ($exe in $candidate.executables) {
        if (Test-Path -LiteralPath (Join-Path $gameRoot $exe) -PathType Leaf) { $game=$candidate;break }
    }
    if ($game) { break }
}
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'dinput8.dll') -PathType Leaf)) { throw '未安装 REFramework' }
# 通用档案只枚举游戏根目录 EXE 做退出检查，从不执行它们。
if (-not $game) {
    $executables=@(Get-ChildItem -LiteralPath $gameRoot -File -Filter '*.exe' | Select-Object -ExpandProperty Name)
    if (-not $executables) { throw '游戏根目录未找到可执行文件' }
    $game=[pscustomobject]@{target='GENERIC';status='untested';executables=$executables}
}
function Assert-GameExited {
    $names=@($game.executables | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) })
    if (Get-Process -Name $names -ErrorAction SilentlyContinue) { throw '请先手动退出游戏，再安装或恢复 REFF' }
# 兼容旧安装的进程检查：防止升级或恢复时改写仍在使用的文件。
    foreach ($process in Get-Process -Name reff-host,reff-webview2-host,reff-webview2-p0 -ErrorAction SilentlyContinue) {
        if ($process.Path -and $process.Path.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) { throw '目标 REFF 宿主仍在运行，请先手动退出游戏' }
    }
}
Assert-GameExited
# 完整校验备份后才恢复；缺失项表示首次安装新增文件，撤销时仅删除该文件。
function Restore-Transaction([string]$Backup,[string[]]$Only=@()) {
    $journal=Get-Content -LiteralPath (Join-Path $Backup 'transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($journal.schemaVersion -ne 1 -or $journal.target -ne $target) { throw '恢复记录与目标不匹配' }
    foreach ($entry in $journal.files) {
        $null=Resolve-REFFFile $target $entry.path
        $saved=Resolve-REFFFile (Join-Path $Backup 'files') $entry.path
        if ($entry.existed -and (Get-FileHash -LiteralPath $saved).Hash -ne $entry.sha256) { throw "备份损坏：$($entry.path)" }
    }
    Assert-GameExited
    foreach ($entry in $journal.files) {
        if ($Only.Count -and $entry.path -notin $Only) { continue }
        $destination=Resolve-REFFFile $target $entry.path
        if ($entry.existed) {
            New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
            Copy-Item -LiteralPath (Resolve-REFFFile (Join-Path $Backup 'files') $entry.path) -Destination $destination -Force
            if ((Get-FileHash -LiteralPath $destination).Hash -ne $entry.sha256) { throw '恢复后哈希不符' }
        } elseif (Test-Path -LiteralPath $destination -PathType Leaf) { Remove-Item -LiteralPath $destination -Force }
    }
}
if ($RestoreBackup) {
    $backupBase=Join-Path $target 'data\REFF\backups'
    $restore=[IO.Path]::GetFullPath($RestoreBackup).TrimEnd('\')
    if ((Split-Path $restore -Parent) -ne $backupBase) { throw '恢复目录必须是本游戏 data/REFF/backups 的直属子目录' }
    $null=Resolve-REFFFile $restore 'reff/package.json'
    Restore-Transaction $restore
    Write-Output "已恢复：$restore";return
}
$source=[IO.Path]::GetFullPath($SourceRoot).TrimEnd('\')
if ($source -eq $target -or $source.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) { throw '先将包解压到临时目录，再运行安装器' }
$package=Get-Content -LiteralPath (Resolve-REFFFile $source 'reff/package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($package.backend -ne 'webview2' -or $package.schemaVersion -ne 1) { throw '不是受支持的 WebView2 分发包' }
$files=@(Read-REFFManifest (Resolve-REFFFile $source 'reff/package-manifest.json') $source -Verify)
if ($files.path -contains 'reff/installed-manifest.json' -or $files.path -contains 'reff/package-manifest.json') { throw '分发清单不能包含安装状态或自身' }
foreach ($required in @('plugins/REFF.dll','reff/runtime/reff-webview2-host.exe','reff/package.json','autorun/REFF.lua','reff/ui/index.html')) {
    if ($required -notin $files.path) { throw "分发清单缺少必要文件：$required" }
}
$files+= [pscustomobject]@{path='reff/package-manifest.json';sha256=(Get-FileHash -LiteralPath (Join-Path $source 'reff/package-manifest.json')).Hash}
$current=@{};foreach ($entry in $files) { $current[$entry.path]=$entry }
$installed=Resolve-REFFFile $target 'reff/installed-manifest.json'
$previous=@();if (Test-Path -LiteralPath $installed) { $previous=Get-Content -LiteralPath $installed -Raw -Encoding UTF8 | ConvertFrom-Json }
$obsolete=@();$preserved=@();$retained=@()
foreach ($old in $previous) {
    # 独立安装的示例保留；旧清单不能授予删除第三方插件的权限。
    if ($old.path -notmatch '^reff/(runtime|ui|licenses|tools|compatibility)/|^reff/(dependencies\.lock|package|package-manifest)\.json$') {
        if ($old.path -match '^(autorun/REFF\.examples\.lua|reff/plugins/example\.(vue|react|html)/)' -and -not $current.ContainsKey($old.path)) {
            $example=Resolve-REFFFile $target $old.path
            if (Test-Path -LiteralPath $example -PathType Leaf) {
                $retained+=[pscustomobject]@{path=$old.path;sha256=(Get-FileHash -LiteralPath $example -Algorithm SHA256).Hash.ToLowerInvariant()}
            }
        };continue
    }
    $destination=Resolve-REFFFile $target $old.path
    if ($current.ContainsKey($old.path) -or -not (Test-Path -LiteralPath $destination -PathType Leaf)) { continue }
    if ($old.sha256 -match '^[a-fA-F0-9]{64}$' -and (Get-FileHash -LiteralPath $destination).Hash -eq $old.sha256) { $obsolete+=$old.path }
    else { $preserved+=$old.path }
}
# 备份所有覆盖项、可证明归属的旧文件及旧清单，备份完整后才修改游戏文件。
$affected=@(@($current.Keys)+$obsolete+'reff/installed-manifest.json' | Sort-Object -Unique)
$backup=Join-Path $target ('data\REFF\backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$null=Resolve-REFFFile $backup 'reff/package.json'
$journal=@()
foreach ($path in $affected) {
    $destination=Resolve-REFFFile $target $path
    $existed=Test-Path -LiteralPath $destination -PathType Leaf
    $hash=$null
    if ($existed) {
        $hash=(Get-FileHash -LiteralPath $destination).Hash
        $saved=Resolve-REFFFile (Join-Path $backup 'files') $path
        New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
        Copy-Item -LiteralPath $destination -Destination $saved
        if ((Get-FileHash -LiteralPath $saved).Hash -ne $hash) { throw '备份后哈希不符' }
    }
    $journal+=[pscustomobject]@{path=$path;existed=[bool]$existed;sha256=$hash}
}
New-Item -ItemType Directory -Path $backup -Force | Out-Null
@{schemaVersion=1;target=$target;files=$journal} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $backup 'transaction.json') -Encoding utf8
Assert-GameExited
$touched=New-Object 'System.Collections.Generic.List[string]'
try {
    foreach ($entry in $files) {
        $destination=Resolve-REFFFile $target $entry.path
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        $touched.Add($entry.path)
        Copy-Item -LiteralPath (Resolve-REFFFile $source $entry.path) -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $destination).Hash -ne $entry.sha256) { throw '安装后哈希不符' }
    }
    foreach ($path in $obsolete) { $touched.Add($path);Remove-Item -LiteralPath (Resolve-REFFFile $target $path) -Force }
    $touched.Add('reff/installed-manifest.json')
    ConvertTo-Json -InputObject @(@($files)+@($retained)) -Depth 8 | Set-Content -LiteralPath $installed -Encoding utf8
    $null=Read-REFFManifest $installed $target -Verify
} catch {
    $cause=$_
    if ($touched.Count) { Restore-Transaction $backup $touched.ToArray() }
    throw $cause
}
[pscustomobject]@{schemaVersion=1;backend='webview2';target=$target;backup=$backup;files=$files.Count;removed=$obsolete;preservedModified=$preserved;legacyOwnershipKnown=[bool]$previous;gameValidated=$false} |
    ConvertTo-Json -Depth 6 | Tee-Object -FilePath (Join-Path $backup 'result.json')
