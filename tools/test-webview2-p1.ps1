param([string]$BuildRoot='',[switch]$Behavior)
$ErrorActionPreference='Stop'
$behaviorMode=[bool]$Behavior
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot=Join-Path $repoRoot 'build-webview2-p1' }
$fixture=Join-Path $repoRoot ('artifacts\webview2-p1\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))

# 测试夹具为全新独立目录，只复制构建产物；附加脚本不进入正式 Shell 或游戏目录。
$ui=Join-Path $fixture 'ui'
$plugins=Join-Path $fixture 'plugins'
New-Item -ItemType Directory -Path $ui,$plugins,(Join-Path $fixture 'outside') -Force | Out-Null
Copy-Item -Path (Join-Path $repoRoot 'web\shell\dist\*') -Destination $ui -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'version.json') -Destination $ui
$browserScript='tests\webview2_browser.js'
if($behaviorMode){$browserScript='tests\webview2_behavior.js'}
Copy-Item -LiteralPath (Join-Path $repoRoot $browserScript) -Destination (Join-Path $ui 'shared\p1-test.js')
foreach($pluginId in @('example.vue','example.react','example.html')) {
    $source=Join-Path $repoRoot ('web\plugins\'+$pluginId)
    $destination=Join-Path $plugins $pluginId
    New-Item -ItemType Directory -Path (Join-Path $destination 'ui\dist') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'manifest.json') -Destination $destination
    Copy-Item -Path (Join-Path $source 'ui\dist\*') -Destination (Join-Path $destination 'ui\dist') -Recurse
}
$entries=@((Join-Path $ui 'index.html'))+@(Get-ChildItem -LiteralPath $plugins -Filter 'index.html' -File -Recurse | ForEach-Object FullName)
foreach($entry in $entries){
    $html=[IO.File]::ReadAllText($entry,[Text.Encoding]::UTF8)
    $html=$html.Replace('</body>','<script src="reff://shell/shared/p1-test.js" defer></script></body>')
    [IO.File]::WriteAllText($entry,$html,(New-Object Text.UTF8Encoding($false)))
}
# 故意放入可读取文件和 junction，验证逐级重解析点检查；这些链接仅属于全新夹具。
[IO.File]::WriteAllText((Join-Path $fixture 'outside\version.json'),'{}')
[IO.File]::WriteAllText((Join-Path $fixture 'outside\index.html'),'<html>outside</html>')
New-Item -ItemType Junction -Path (Join-Path $ui 'escape') -Target (Join-Path $fixture 'outside') | Out-Null
New-Item -ItemType Junction -Path (Join-Path $ui 'link-root') -Target (Join-Path $fixture 'outside') | Out-Null
& (Join-Path $BuildRoot 'Release\reff-wv2-services.exe') $fixture
if($LASTEXITCODE -ne 0){throw "P1 服务边界测试失败：$fixture"}
$driverArgs=@($repoRoot,$fixture,(Join-Path $BuildRoot 'Release\reff-webview2-host.exe'))
if($behaviorMode){
    $config=Join-Path $fixture 'data\REFF'
    New-Item -ItemType Directory -Path $config -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $config 'development.json'),'{"enabled":true}')
    [IO.File]::WriteAllText((Join-Path $plugins 'example.vue\.reff-dev-version'),'initial')
    $driverArgs+='behavior'
}
& (Join-Path $BuildRoot 'Release\reff-wv2-integration.exe') @driverArgs *> (Join-Path $fixture 'integration.log')
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $fixture 'integration.log') -Tail 25;throw "P1 浏览器联调失败：$fixture"}
$summary=Get-Content -LiteralPath (Join-Path $fixture 'integration-summary.json') -Raw | ConvertFrom-Json
$summary | Select-Object passed,frames,width,height,luaSubscriptionCleanup,gracefulExit
Write-Host "P1 独立证据：$fixture"
if($behaviorMode){return}

# 渲染/故障使用另一份极小资源夹具，避免业务测试脚本的超时干扰；仍走正式受控协议。
$renderUi=Join-Path $fixture 'render\ui'
$renderPlugins=Join-Path $fixture 'render\plugins'
New-Item -ItemType Directory -Path $renderUi,$renderPlugins -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $renderUi 'index.html'),'<html><body style="margin:0;background:transparent"><div style="position:fixed;left:16px;top:16px;width:80px;height:80px;background:#40a0e0"></div><span id="counter" style="position:fixed;left:120px;top:20px"></span><script>let n=0;setInterval(()=>counter.textContent=++n,16)</script></body></html>',(New-Object Text.UTF8Encoding($false)))
& (Join-Path $BuildRoot 'Release\reff-wv2-dx12.exe') (Join-Path $BuildRoot 'Release\reff-webview2-host.exe') (Join-Path $fixture 'dx12') $renderUi $renderPlugins *> (Join-Path $fixture 'dx12.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $fixture 'dx12.log') -Tail 20;throw "P1 DX12 验证失败：$fixture"}
& (Join-Path $BuildRoot 'Release\reff-wv2-fault.exe') (Join-Path $BuildRoot 'Release\reff-webview2-host.exe') (Join-Path $fixture 'fault') $renderUi $renderPlugins *> (Join-Path $fixture 'fault.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $fixture 'fault.log') -Tail 20;throw "P1 故障验证失败：$fixture"}
Write-Host 'P1 真实 DX12/连续缩放及七项故障自检通过。'
