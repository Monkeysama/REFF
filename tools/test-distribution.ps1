param([string]$SourceRoot='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $SourceRoot) { $SourceRoot=Join-Path $repoRoot 'staging/reframework' }
. (Join-Path $PSScriptRoot 'package-files.ps1')
$fixture=Join-Path $repoRoot ('artifacts/webview2-p4/install-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
# 伪游戏仅有普通文本文件，从不运行 EXE；安装器仍使用正式目录识别、进程与哈希校验。
function New-InstallFixture([string]$Name,[string]$Executable='re4.exe') {
    $root=Join-Path $fixture $Name
    New-Item -ItemType Directory -Path (Join-Path $root 'reframework/reff') -Force | Out-Null
    'fixture only' | Set-Content -LiteralPath (Join-Path $root $Executable)
    'fixture only' | Set-Content -LiteralPath (Join-Path $root 'dinput8.dll')
    return (Join-Path $root 'reframework')
}
function Put-FixtureFile([string]$Root,[string]$Path,[string]$Content) {
    $dest=Join-Path $Root $Path
    New-Item -ItemType Directory -Path (Split-Path $dest) -Force | Out-Null
    [IO.File]::WriteAllText($dest,$Content)
}
function Get-FixtureFiles([string]$Root) {
    $result=@{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse | Where-Object { $_.FullName -notlike '*\data\REFF\backups\*' }) {
        $result[$file.FullName.Substring($Root.Length+1)]=(Get-FileHash -LiteralPath $file.FullName).Hash
    }
    return $result
}
function Assert-Fixture([bool]$Pass,[string]$Message) { if (-not $Pass) { throw $Message } }
function Assert-SameFiles($Before,$After) {
    Assert-Fixture ($Before.Count -eq $After.Count) '恢复后文件数不符'
    foreach ($key in $Before.Keys) { Assert-Fixture ($After[$key] -eq $Before[$key]) "恢复后哈希不符：$key" }
}
$installer=Join-Path $SourceRoot 'reff/tools/install-reff.ps1'
$clean=New-InstallFixture 'clean'
Put-FixtureFile $clean 'data/REFF/settings.json' '{"language":"en-US"}'
Put-FixtureFile $clean 'reff/plugins/thirdparty/index.html' 'thirdparty'
$before=Get-FixtureFiles $clean
# 实际使用随包 -File 入口且不传 SourceRoot，验证默认路径在原生 PowerShell 5.1 可用。
$result=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer -ReframeworkRoot $clean | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw '随包安装器 -File 入口失败' }
$null=Read-REFFManifest (Join-Path $clean 'reff/installed-manifest.json') $clean -Verify
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer -ReframeworkRoot $clean -RestoreBackup $result.backup | Out-Null
if ($LASTEXITCODE -ne 0) { throw '随包恢复 -File 入口失败' }
Assert-SameFiles $before (Get-FixtureFiles $clean)
# 已知但未测试游戏与目录外新目标均能安装、恢复；夹具 EXE 是文本且从不运行。
foreach ($exe in @('DevilMayCry5.exe','FutureREGame.exe')) {
    $generic=New-InstallFixture ('all-games-'+$exe) $exe
    $genericBefore=Get-FixtureFiles $generic
    $genericResult=& $installer -ReframeworkRoot $generic -SourceRoot $SourceRoot | ConvertFrom-Json
    $null=Read-REFFManifest (Join-Path $generic 'reff/installed-manifest.json') $generic -Verify
    & $installer -ReframeworkRoot $generic -RestoreBackup $genericResult.backup | Out-Null
    Assert-SameFiles $genericBefore (Get-FixtureFiles $generic)
}
$upgrade=New-InstallFixture 'cef-upgrade'
foreach ($pair in @(
    @('plugins/REFF.dll','old-dll'),@('autorun/REFF.lua','old-lua'),@('reff/runtime/libcef.dll','cef'),
    @('reff/runtime/reff-host.exe','old-host'),@('reff/ui/assets/old.js','old-ui'),
    @('reff/runtime/user-modified.pak','original'),@('reff/licenses/CEF.txt','old-license'),
    @('reff/plugins/example.vue/manifest.json','example'),@('autorun/REFF.examples.lua','example-lua')
)) { Put-FixtureFile $upgrade $pair[0] $pair[1] }
$old=Write-REFFManifest $upgrade (Join-Path $upgrade 'reff/installed-manifest.json')
# 即便旧清单声称拥有第三方插件，也必须保留。
Put-FixtureFile $upgrade 'reff/plugins/thirdparty/index.html' 'thirdparty'
$third=Join-Path $upgrade 'reff/plugins/thirdparty/index.html'
$old+= [pscustomobject]@{path='reff/plugins/thirdparty/index.html';sha256=(Get-FileHash -LiteralPath $third).Hash}
ConvertTo-Json -InputObject @($old) | Set-Content -LiteralPath (Join-Path $upgrade 'reff/installed-manifest.json') -Encoding UTF8
Put-FixtureFile $upgrade 'reff/runtime/user-modified.pak' 'modified by user'
Put-FixtureFile $upgrade 'data/REFF/settings.json' '{"language":"en-US"}'
$before=Get-FixtureFiles $upgrade
# 示例可能由独立增量包更新或被用户删除；升级不得因旧示例哈希失效而整包回滚。
Put-FixtureFile $upgrade 'reff/plugins/example.vue/manifest.json' 'example edited separately'
Remove-Item -LiteralPath (Join-Path $upgrade 'autorun/REFF.examples.lua')
$before=Get-FixtureFiles $upgrade
$result=& $installer -ReframeworkRoot $upgrade -SourceRoot $SourceRoot | ConvertFrom-Json
Assert-Fixture (-not (Test-Path -LiteralPath (Join-Path $upgrade 'reff/runtime/libcef.dll'))) '旧 CEF 未清理'
Assert-Fixture ('reff/runtime/user-modified.pak' -in $result.preservedModified) '修改文件未保留'
Assert-Fixture ((Get-FileHash -LiteralPath $third).Hash -eq $before['reff\plugins\thirdparty\index.html']) '第三方文件被修改'
Assert-Fixture ((Get-FileHash -LiteralPath (Join-Path $upgrade 'reff/plugins/example.vue/manifest.json')).Hash -eq $before['reff\plugins\example.vue\manifest.json']) '独立示例被修改'
& $installer -ReframeworkRoot $upgrade -RestoreBackup $result.backup | Out-Null
Assert-SameFiles $before (Get-FixtureFiles $upgrade)
# 在真实安装复制的第三项注入一次 I/O 失败，覆盖事务中途回滚分支；备份/恢复走系统 Copy-Item。
$rollback=New-InstallFixture 'rollback'
Put-FixtureFile $rollback 'plugins/REFF.dll' 'old-dll'
$before=Get-FixtureFiles $rollback
$global:REFFInstallFaultRoot=$rollback;$global:REFFInstallFaultCount=0
function global:Copy-Item {
    param([string]$LiteralPath,[string]$Destination,[switch]$Force)
    if ($Destination.StartsWith($global:REFFInstallFaultRoot+'\') -and $Destination -notlike '*\data\REFF\backups\*') {
        $global:REFFInstallFaultCount++
        if ($global:REFFInstallFaultCount -eq 3) { throw 'distribution fixture injected I/O failure' }
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
}
$failed=$false
try { & $installer -ReframeworkRoot $rollback -SourceRoot $SourceRoot | Out-Null }
catch { Assert-Fixture ($_.ToString() -match 'injected I/O failure') $_.ToString();$failed=$true }
finally { Remove-Item Function:\Copy-Item;Remove-Variable REFFInstallFaultRoot,REFFInstallFaultCount -Scope Global }
Assert-Fixture $failed '未触发安装失败'
Assert-SameFiles $before (Get-FixtureFiles $rollback)
# 路径穿越、junction、重复清单必须在写入前拒绝。
foreach ($bad in @('../plugins/REFF.dll','reff/runtime/../../other.dll','reff/ui/../outside','reff/ui/a:stream','reff/ui/file.','reff/ui//file')) {
    $rejected=$false;try { $null=Resolve-REFFFile $upgrade $bad } catch { $rejected=$true }
    Assert-Fixture $rejected "路径未拒绝：$bad"
}
$outside=Join-Path $fixture 'outside';New-Item -ItemType Directory -Path $outside | Out-Null
New-Item -ItemType Junction -Path (Join-Path $rollback 'reff/ui') -Target $outside | Out-Null
$rejected=$false;try { & $installer -ReframeworkRoot $rollback -SourceRoot $SourceRoot | Out-Null } catch { $rejected=$_.ToString() -match '重解析' }
Assert-Fixture $rejected 'junction 未拒绝'
# 诊断读取带敏感哨兵的假日志，只能输出白名单字段；不复制任意日志/输入内容。
$diagnostic=New-InstallFixture 'diagnostic'
$null=& $installer -ReframeworkRoot $diagnostic -SourceRoot $SourceRoot
Put-FixtureFile $diagnostic 'data/REFF/log/webview2/summary.json' '{"runtime":"155.0.4283.62","osBuild":26300,"publishedFrames":17,"error":"secret-input-sentinel","clipboard":"secret-input-sentinel"}'
Put-FixtureFile $diagnostic 'data/REFF/log/compatibility.log' 'target=RE4 status=untested initialization=complete secret-input-sentinel'
& (Join-Path $SourceRoot 'reff/tools/collect-installed-compatibility.ps1') -ReframeworkRoot $diagnostic | Out-Null
$diagnosticReport=Get-ChildItem -LiteralPath (Join-Path $diagnostic 'data/REFF/diagnostics') -Filter report.json -Recurse -File | Select-Object -First 1
$safeText=[IO.File]::ReadAllText($diagnosticReport.FullName)
Assert-Fixture (-not $safeText.Contains('secret-input-sentinel')) '诊断泄漏输入内容'
$safeReport=$safeText | ConvertFrom-Json
Assert-Fixture ($safeReport.host.runtime -eq '155.0.4283.62' -and $safeReport.host.publishedFrames -eq 17 -and $safeReport.native.target -eq 'RE4') '诊断未收集规定字段'
[ordered]@{passed=$true;allGameInstall=$true;genericTargetInstall=$true;cleanInstall=$true;powershell51FileEntry=$true;cefUpgrade=$true;restoreHashes=$true;preserveSettingsAndThirdParty=$true;preserveModified=$true;preserveChangedOrMissingExamples=$true;rollback=$true;pathAndJunctionRejected=$true;diagnosticsAllowlist=$true;gameValidated=$false;fixture=$fixture} |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $fixture 'summary.json')
