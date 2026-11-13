param(
    [string]$BuildRoot='',
    [switch]$IncludeExamples,
    [string]$StagingRoot=''
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot=Join-Path $repoRoot 'build-webview2-release' }
if (-not $StagingRoot) { $StagingRoot=Join-Path $repoRoot 'staging' }
. (Join-Path $PSScriptRoot 'package-files.ps1')
$StagingRoot=[IO.Path]::GetFullPath($StagingRoot).TrimEnd('\')
$stageRoot=Join-Path $StagingRoot 'reframework'
$cache=Join-Path $BuildRoot 'CMakeCache.txt'
foreach ($flag in @('REFF_ENABLE_WEBVIEW2','REFF_ENABLE_IME_PROXY')) {
    if (-not (Select-String -LiteralPath $cache -Pattern "^${flag}:BOOL=ON$" -Quiet)) { throw "分发构建缺少 $flag" }
}
# 输出规范化到 staging 内；只重建 reframework 生成目录，不清空调用者的父目录。
$stagingBase=[IO.Path]::GetFullPath((Join-Path $repoRoot 'staging')).TrimEnd('\')
if ($StagingRoot -ne $stagingBase -and (Split-Path $StagingRoot -Parent) -ne $stagingBase) { throw 'StagingRoot 必须是仓库 staging 或其直属子目录' }
$null=Resolve-REFFFile $stageRoot 'reff/package.json'
if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
function Copy-ReleaseFile([string]$Source,[string]$Relative) {
    $dest=Resolve-REFFFile $stageRoot $Relative
    New-Item -ItemType Directory -Path (Split-Path $dest) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $dest -Force
}
function Copy-ReleaseTree([string]$Source,[string]$Relative) {
    $base=[IO.Path]::GetFullPath($Source).TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $base -File -Recurse) {
        Copy-ReleaseFile $file.FullName ($Relative+'/'+$file.FullName.Substring($base.Length+1).Replace('\','/'))
    }
}
Copy-ReleaseFile (Join-Path $BuildRoot 'Release/REFF.dll') 'plugins/REFF.dll'
Copy-ReleaseFile (Join-Path $BuildRoot 'Release/reff-webview2-host.exe') 'reff/reff-webview2-host.exe'
foreach ($path in @('REFF.lua','reff/bridge.lua','reff/sdk.lua')) { Copy-ReleaseFile (Join-Path $repoRoot ('lua/'+$path)) ('autorun/'+$path) }
# 网页、版本和依赖来自实际构建 EXE，不再复制 ui/ 或外置锁文件。
$infoFile=Join-Path $StagingRoot 'embedded-info.json'
$hostFile=Join-Path $stageRoot 'reff/reff-webview2-host.exe'
$process=Start-Process -FilePath $hostFile -ArgumentList ('--reff-resource-info="'+$infoFile+'"') -WindowStyle Hidden -PassThru
$null=$process.Handle;$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw '无法读取宿主内嵌资源元数据' }
$embedded=Get-Content -LiteralPath $infoFile -Raw -Encoding UTF8 | ConvertFrom-Json
$expected=(Get-Content -LiteralPath (Join-Path $repoRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
if (-not $embedded.embeddedShell -or $embedded.version.version -ne $expected) { throw '宿主内嵌网页版本不符，请重新构建原生宿主' }
# 所有许可及 NOTICE 外置合并保留，方便离线阅读。
$licenseFiles=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'web/shell/node_modules/.pnpm') -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|LICENCE|NOTICE)(\..*)?$' } | Sort-Object FullName)
if (-not $licenseFiles) { throw '缺少 Web 依赖许可；先安装锁定 pnpm 依赖' }
$licenseText=New-Object Text.StringBuilder
foreach ($pair in @(@('REFF','LICENSE'),@('REFramework','.deps/reframework/LICENSE'),@('WebView2','.deps/webview2/LICENSE.txt'),@('WebView2 NOTICE','.deps/webview2/NOTICE.txt'))) {
    $null=$licenseText.AppendLine('===== '+$pair[0]+' =====').AppendLine([IO.File]::ReadAllText((Join-Path $repoRoot $pair[1]))).AppendLine()
}
foreach ($file in $licenseFiles) {
    $null=$licenseText.AppendLine($file.FullName.Substring($repoRoot.Length+1)).AppendLine([IO.File]::ReadAllText($file.FullName)).AppendLine()
}
$notice=Resolve-REFFFile $stageRoot 'reff/THIRD-PARTY-NOTICES.txt'
New-Item -ItemType Directory -Path (Split-Path $notice) -Force | Out-Null
[IO.File]::WriteAllText($notice,$licenseText.ToString(),(New-Object Text.UTF8Encoding($false)))
# 维护脚本只供本地开发部署及安装自检；不压缩、上传或放入游戏目录。
$toolsRoot=Join-Path $StagingRoot 'tools'
$null=Resolve-REFFFile $toolsRoot 'reff/package.json'
New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
foreach ($tool in @('install-reff.ps1','package-files.ps1','collect-installed-compatibility.ps1')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $tool) -Destination $toolsRoot -Force }
Copy-Item -LiteralPath (Join-Path $repoRoot 'config/game-support.json') -Destination $toolsRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TOOLS-README.md') -Destination (Join-Path $toolsRoot 'README.md') -Force
$version=(Get-Content -LiteralPath (Join-Path $repoRoot 'version.json') -Raw | ConvertFrom-Json).version
[ordered]@{schemaVersion=1;backend='webview2';version=$version;embeddedShellVersion=$embedded.version.version;resources='embedded';supportScope='all-reframework-games';minimumWindowsBuild=19045;minimumRuntime=$embedded.dependencies.webview2.minimumRuntime;runtimeDistribution='Evergreen shared stable';runtimeIncluded=$false;runtimeDownload='https://developer.microsoft.com/microsoft-edge/webview2/';manualGameValidation='pending';createdUtc=[DateTime]::UtcNow.ToString('o')} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stageRoot 'reff/package.json') -Encoding utf8
if ($IncludeExamples) {
    Copy-ReleaseFile (Join-Path $repoRoot 'examples/lua/REFF.examples.lua') 'autorun/REFF.examples.lua'
    foreach ($id in @('example.vue','example.react','example.html')) {
        Copy-ReleaseFile (Join-Path $repoRoot "web/plugins/$id/manifest.json") "reff/plugins/$id/manifest.json"
        Copy-ReleaseTree (Join-Path $repoRoot "web/plugins/$id/ui/dist") "reff/plugins/$id/ui/dist"
    }
}
$null=Write-REFFManifest $stageRoot (Join-Path $stageRoot 'reff/package-manifest.json')
$manifest=Write-REFFManifest $stageRoot (Join-Path $StagingRoot 'manifest.json')
$bytes=($manifest | Measure-Object bytes -Sum).Sum
if ($bytes -gt 30MB -or (1-$bytes/(402MB)) -lt .9) { throw "包体积超标：$bytes bytes" }
[pscustomobject]@{schemaVersion=1;backend='webview2';supportScope='all-reframework-games';buildRoot=[IO.Path]::GetFullPath($BuildRoot);bytes=$bytes;baselineBytes=402MB;reduction=1-$bytes/(402MB)} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $StagingRoot 'build-profile.json') -Encoding utf8
Write-Output "WebView2 staging：$stageRoot，$($manifest.Count) 文件，$bytes bytes；不含共享 Runtime、缓存和旧版备份。"
