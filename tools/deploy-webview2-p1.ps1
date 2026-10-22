param([string]$ReframeworkRoot='C:\Steam\steamapps\common\MonsterHunterWilds\reframework',[string]$StagingRoot)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'game-catalog.ps1')
. (Join-Path $PSScriptRoot 'webview2-p1-package.ps1')
$target=[IO.Path]::GetFullPath($ReframeworkRoot).TrimEnd('\')
$gameRoot=Split-Path $target -Parent
$game=Find-REFFGame $repoRoot $gameRoot @('tested', 'untested')
if((Split-Path $target -Leaf) -ne 'reframework' -or -not (Test-Path -LiteralPath (Join-Path $gameRoot 'dinput8.dll'))){throw '目标必须是已安装 REFramework 的游戏'}
$processNames=if($game){@($game.processName)}else{@(Get-ChildItem -LiteralPath $gameRoot -File -Filter '*.exe' | ForEach-Object {[IO.Path]::GetFileNameWithoutExtension($_.Name)})}
if(-not $processNames){throw '游戏根目录没有可用于退出检查的 EXE'}
if(-not $game){$game=[pscustomobject]@{target='GENERIC';status='untested'}}
if(Get-Process -Name $processNames -ErrorAction SilentlyContinue){throw '检测到游戏仍在运行，请手动退出后再部署'}
if(-not $StagingRoot){$StagingRoot=(Get-Content -LiteralPath (Join-Path $repoRoot 'artifacts/webview2-p1/stage-latest.json') -Raw | ConvertFrom-Json).stage}
$sourceRoot=Join-Path $StagingRoot 'reframework'
# Windows PowerShell 5.1 会把 ConvertFrom-Json 的数组作为单个管道对象，不再用 @() 二次包装。
$files=Get-Content -LiteralPath (Join-Path $StagingRoot 'manifest.json') -Raw | ConvertFrom-Json
if(-not $files.Count){throw 'P1 包清单为空'}

# 全量预检后备份本次覆盖项；保留现有旧宿主、用户设置及第三方插件，恢复无需猜测文件归属。
foreach($file in $files){
    $source=Resolve-REFFP1Path $sourceRoot $file.path
    $null=Resolve-REFFP1Path $target $file.path
    if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $file.sha256){throw "包哈希不一致：$($file.path)"}
}
$backup=Join-Path $repoRoot ('artifacts/webview2-p1/backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$installed=Join-Path $target 'reff/installed-manifest.json'
$owned=@{}
if(Test-Path -LiteralPath $installed){
    Copy-Item -LiteralPath $installed -Destination (Join-Path $backup 'installed-manifest.json')
    foreach($entry in (Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json)){$owned[$entry.path]=$true}
}
$changes=@(foreach($file in $files){
    $destination=Resolve-REFFP1Path $target $file.path
    $existed=Test-Path -LiteralPath $destination -PathType Leaf
    if($existed){$copy=Resolve-REFFP1Path $backup $file.path;New-Item -ItemType Directory -Path (Split-Path $copy) -Force | Out-Null;Copy-Item -LiteralPath $destination -Destination $copy}
    [pscustomobject]@{path=$file.path;existed=$existed}
})
[pscustomobject]@{targetRoot=$target;files=$changes} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'restore-info.json') -Encoding utf8
if(Get-Process -Name $processNames -ErrorAction SilentlyContinue){throw '部署前检测到游戏已启动，停止覆盖'}
foreach($file in $files){
    $destination=Resolve-REFFP1Path $target $file.path
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Resolve-REFFP1Path $sourceRoot $file.path) -Destination $destination -Force
    if((Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant() -ne $file.sha256){throw "部署哈希不一致：$($file.path)"}
    $owned[$file.path]=$true
}
$manifest=@(foreach($relative in ($owned.Keys | Sort-Object)){
    $full=[IO.Path]::GetFullPath((Join-Path $target $relative))
    if(-not $full.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw '安装清单路径越界'}
    if(Test-Path -LiteralPath $full -PathType Leaf){[pscustomobject]@{path=$relative;bytes=(Get-Item -LiteralPath $full).Length;sha256=(Get-FileHash -LiteralPath $full).Hash.ToLowerInvariant()}}
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $installed -Encoding utf8
[pscustomobject]@{backend='webview2-p1';game=$game.target;backup=$backup;files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
Write-Output "P1 已部署并逐文件核对哈希；备份：$backup"
$files | Where-Object path -in @('plugins/REFF.dll','reff/runtime/reff-webview2-host.exe') | Select-Object path,sha256
