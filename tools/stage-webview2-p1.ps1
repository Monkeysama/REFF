param([switch]$IncludeExamples,[ValidateSet('P1','P2')][string]$Phase='P1')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$phaseDirectory='webview2-'+$Phase.ToLowerInvariant()
New-Item -ItemType Directory -Path (Join-Path $repoRoot ('artifacts/'+$phaseDirectory)) -Force | Out-Null
. (Join-Path $PSScriptRoot 'webview2-p1-package.ps1')
$build=Join-Path $repoRoot 'build-webview2-p1-game'
foreach($flag in @('REFF_ENABLE_WEBVIEW2','REFF_ENABLE_IME_PROXY')){
    if(-not (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern "^${flag}:BOOL=ON$" -Quiet)){throw "P1 构建缺少 $flag"}
}
$stage=Join-Path $repoRoot ('staging/'+$phaseDirectory+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$root=Join-Path $stage 'reframework'

# 每次创建新 staging，仅复制明确产物；不携带旧引擎、缓存、node_modules 或测试注入脚本。
function Copy-P1File([string]$Source,[string]$Relative){
    $destination=Resolve-REFFP1Path $root $Relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot $Source) -Destination $destination -Force
}
function Copy-P1Tree([string]$Source,[string]$Relative){
    $sourceRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot $Source)).TrimEnd('\')
    foreach($file in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse){
        $suffix=$file.FullName.Substring($sourceRoot.Length+1).Replace('\','/')
        Copy-P1File ($Source+'/'+$suffix) ($Relative+'/'+$suffix)
    }
}
Copy-P1File 'build-webview2-p1-game/Release/REFF.dll' 'plugins/REFF.dll'
Copy-P1File 'build-webview2-p1/Release/reff-webview2-host.exe' 'reff/runtime/reff-webview2-host.exe'
Copy-P1File 'lua/REFF.lua' 'autorun/REFF.lua'
Copy-P1File 'lua/reff/bridge.lua' 'autorun/reff/bridge.lua'
Copy-P1File 'lua/reff/sdk.lua' 'autorun/reff/sdk.lua'
Copy-P1Tree 'web/shell/dist' 'reff/ui'
Copy-P1File 'version.json' 'reff/ui/version.json'
Copy-P1File 'LICENSE' 'reff/licenses/REFF.txt'
Copy-P1File '.deps/reframework/LICENSE' 'reff/licenses/REFramework.txt'
Copy-P1File '.deps/webview2/LICENSE.txt' 'reff/licenses/WebView2.txt'
Copy-P1File '.deps/webview2/NOTICE.txt' 'reff/licenses/WebView2-NOTICE.txt'
Copy-P1File 'dependencies.lock.json' 'reff/dependencies.lock.json'
if($IncludeExamples){
    Copy-P1File 'examples/lua/REFF.examples.lua' 'autorun/REFF.examples.lua'
    foreach($id in @('example.vue','example.react','example.html')){
        Copy-P1File "web/plugins/$id/manifest.json" "reff/plugins/$id/manifest.json"
        Copy-P1Tree "web/plugins/$id/ui/dist" "reff/plugins/$id/ui/dist"
    }
}
$manifest=@(Get-ChildItem -LiteralPath $root -File -Recurse | ForEach-Object {
    [pscustomobject]@{path=$_.FullName.Substring($root.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $repoRoot ('docs/'+$phaseDirectory+'-validation.zh-CN.md')) -Destination (Join-Path $stage ('README-'+$Phase+'.zh-CN.md'))
$archive=Join-Path $repoRoot ('artifacts/REFF-WebView2-'+$Phase+'-game-test.zip')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
"$((Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant())  REFF-WebView2-$Phase-game-test.zip" | Set-Content -LiteralPath ($archive+'.sha256') -Encoding ascii
$info=[pscustomobject]@{stage=$stage;archive=$archive;includeExamples=[bool]$IncludeExamples;files=$manifest.Count;bytes=($manifest | Measure-Object bytes -Sum).Sum;systemRuntimeIncluded=$false;gameValidated=$false}
$info | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $repoRoot ('artifacts/'+$phaseDirectory+'/stage-latest.json')) -Encoding utf8
$info
