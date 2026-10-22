param([Parameter(Mandatory=$true)][string]$BackupDirectory)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'game-catalog.ps1')
. (Join-Path $PSScriptRoot 'webview2-p1-package.ps1')
$backup=[IO.Path]::GetFullPath($BackupDirectory)
$info=Get-Content -LiteralPath (Join-Path $backup 'restore-info.json') -Raw | ConvertFrom-Json
$target=[IO.Path]::GetFullPath($info.targetRoot).TrimEnd('\')
$game=Find-REFFGame $repoRoot (Split-Path $target -Parent) @('tested', 'untested')
if((Split-Path $target -Leaf) -ne 'reframework'){throw '备份目标必须是 reframework 目录'}
$processNames=if($game){@($game.processName)}else{@(Get-ChildItem -LiteralPath (Split-Path $target -Parent) -File -Filter '*.exe' | ForEach-Object {[IO.Path]::GetFileNameWithoutExtension($_.Name)})}
if(-not $processNames){throw '游戏根目录没有可用于退出检查的 EXE'}
if(Get-Process -Name $processNames -ErrorAction SilentlyContinue){throw '请手动退出游戏后恢复'}

# 只恢复经过白名单及完整性预检的单文件；新增项逐文件删除，不递归清理目录。
foreach($file in $info.files){
    $null=Resolve-REFFP1Path $target $file.path
    $source=Resolve-REFFP1Path $backup $file.path
    if($file.existed -and -not (Test-Path -LiteralPath $source -PathType Leaf)){throw '恢复备份不完整'}
}
foreach($file in $info.files){
    $destination=Resolve-REFFP1Path $target $file.path
    if($file.existed){
        $source=Resolve-REFFP1Path $backup $file.path
        Copy-Item -LiteralPath $source -Destination $destination -Force
        if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash){throw '恢复哈希不一致'}
    }elseif(Test-Path -LiteralPath $destination){Remove-Item -LiteralPath $destination -Force}
}
$manifest=Join-Path $target 'reff/installed-manifest.json'
if(Test-Path -LiteralPath (Join-Path $backup 'installed-manifest.json')){Copy-Item -LiteralPath (Join-Path $backup 'installed-manifest.json') -Destination $manifest -Force}
elseif(Test-Path -LiteralPath $manifest){Remove-Item -LiteralPath $manifest -Force}
Write-Output '已恢复 P1 部署前的文件及安装清单。'
