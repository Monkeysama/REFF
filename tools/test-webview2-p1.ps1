param([string]$BuildRoot='',[switch]$Behavior,[switch]$Layout,[switch]$Preview,[switch]$ProductionPreview)
$ErrorActionPreference='Stop'
$behaviorMode=[bool]$Behavior -or [bool]$Layout -or [bool]$Preview
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot=Join-Path $repoRoot 'build-webview2-p1' }
$fixture=Join-Path $repoRoot ('artifacts\webview2-p1\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))

if($ProductionPreview){
    # 正式 Shell 恢复已选插件；无 selftest、测试脚本注入或网页 DOM 自动操作。
    $plugin=Join-Path $fixture 'plugins/example.vue'
    New-Item -ItemType Directory -Path $plugin -Force|Out-Null
    [IO.File]::WriteAllText((Join-Path $plugin 'manifest.json'),'{"manifestVersion":1,"id":"example.vue","name":"Production Preview","version":"1.0.1","reffApi":">=1.0.0 <2.0.0","games":["MHWILDS"],"ui":{"kind":"page","mode":"isolated-page","entry":"index.html"},"methods":["example.vue.get"],"events":[],"fallback":"none"}')
    [IO.File]::WriteAllText((Join-Path $plugin 'index.html'),'<html><head><script type="module" src="app.mjs"></script></head><body style="background:transparent;margin:0"><div id="portrait" style="position:absolute;left:30px;top:80px;width:240px;height:300px"></div></body></html>')
    [IO.File]::WriteAllText((Join-Path $plugin 'app.mjs'),"import {registerNativePreviewSlot} from 'reff://shell/shared/reff-ui.mjs';const call=(m,p={})=>window.reffNativeCall(m,p);while(!(await call('ui.status')).ready)await new Promise(r=>setTimeout(r,25));await registerNativePreviewSlot({call},document.getElementById('portrait'),'portrait');await call('example.vue.get');")
    & (Join-Path $BuildRoot 'Release/reff-wv2-integration.exe') $repoRoot $fixture (Join-Path $BuildRoot 'Release/reff-webview2-host.exe') production-preview *> (Join-Path $fixture 'integration.log')
    if($LASTEXITCODE -ne 0){throw "正式宿主预览验证失败：$fixture"}
    Write-Host "正式宿主预览验证通过：$fixture";return
}

# 测试夹具为全新独立目录，只复制构建产物；附加脚本不进入正式 Shell 或游戏目录。
$ui=Join-Path $fixture 'ui'
$plugins=Join-Path $fixture 'plugins'
New-Item -ItemType Directory -Path $ui,$plugins,(Join-Path $fixture 'outside') -Force | Out-Null
Copy-Item -Path (Join-Path $repoRoot 'web\shell\dist\*') -Destination $ui -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'version.json') -Destination $ui
$browserScript='tests\webview2_browser.js'
if($behaviorMode){$browserScript='tests\webview2_behavior.js'}
if($Preview){$browserScript='tests\webview2_preview.js'}
Copy-Item -LiteralPath (Join-Path $repoRoot $browserScript) -Destination (Join-Path $ui 'shared\p1-test.js')
foreach($pluginId in @('example.vue','example.react','example.html')) {
    $source=Join-Path $repoRoot ('web\plugins\'+$pluginId)
    $destination=Join-Path $plugins $pluginId
    New-Item -ItemType Directory -Path (Join-Path $destination 'ui\dist') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'manifest.json') -Destination $destination
    Copy-Item -Path (Join-Path $source 'ui\dist\*') -Destination (Join-Path $destination 'ui\dist') -Recurse
}
# 测试脚本由独立宿主注入，Shell/公共资源使用实际 EXE 内嵌版本，不改写网页产物。
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
    $driverArgs+=if($Preview){'preview'}else{'behavior'}
}
if($Layout){
    # 四次独立进程启动共用 Core 设置文件；最后仅改名测试夹具清单，模拟插件卸载。
    foreach($phase in 0..3){
        $script=Get-Content -LiteralPath (Join-Path $repoRoot 'tests\webview2_layout.js') -Raw -Encoding UTF8
        [IO.File]::WriteAllText((Join-Path $ui 'shared\p1-test.js'),$script.Replace('__LAYOUT_PHASE__',[string]$phase),(New-Object Text.UTF8Encoding($false)))
        if($phase -eq 3){Rename-Item -LiteralPath (Join-Path $plugins 'example.react\manifest.json') -NewName 'manifest.uninstalled'}
        & (Join-Path $BuildRoot 'Release\reff-wv2-integration.exe') @driverArgs *> (Join-Path $fixture ('layout-phase-'+$phase+'.log'))
        if($LASTEXITCODE -ne 0){Get-Content (Join-Path $fixture ('layout-phase-'+$phase+'.log')) -Tail 25;throw "布局/重启验证失败：$fixture phase=$phase"}
        Copy-Item -LiteralPath (Join-Path $fixture 'integration-summary.json') -Destination (Join-Path $fixture ('layout-phase-'+$phase+'.json'))
    }
    Write-Host "布局/四次真实宿主启动验证通过：$fixture"
    return
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
