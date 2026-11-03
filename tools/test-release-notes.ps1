$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $repoRoot ('artifacts/release-notes-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
. (Join-Path $PSScriptRoot 'release-notes.ps1')
function Assert-Notes([bool]$Pass,[string]$Reason) { if (-not $Pass) { throw $Reason } }
function Assert-NotesRejected([scriptblock]$Action) {
    $rejected=$false;try { & $Action | Out-Null } catch { $rejected=$true }
    Assert-Notes $rejected '发布说明契约未拒绝无效输入'
}
# 版本边界、子标题和完整历史分别验证；不能将 1.0.10 误认为 1.0.1。
$history=Join-Path $fixture 'CHANGELOG.md'
[IO.File]::WriteAllText($history,"# 更新日志`r`n`r`n## v1.0.10 — 将来`r`n将来内容`r`n## v1.0.1 — 当前`r`n### 功能`r`n当前内容`r`n## v1.0.0 — 旧版`r`n旧版内容")
$notes=Get-REFFReleaseNotes $history '1.0.1'
Assert-Notes ($notes -match '当前内容' -and $notes -match '### 功能' -and $notes -notmatch '将来内容|旧版内容') '版本章节提取错误'
Assert-NotesRejected { Get-REFFReleaseNotes $history '2.0.0' }
[IO.File]::AppendAllText($history,"`r`n## v1.0.1 — 重复`r`n重复内容")
Assert-NotesRejected { Get-REFFReleaseNotes $history '1.0.1' }
$global:reffReleaseNotesTest=@{
    version=[string](Get-Content (Join-Path $repoRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
    calls=(New-Object 'System.Collections.Generic.List[object]')
    hasTools=$true;failView=$false;failDelete=$false
}
# 用同名 CLI 函数代替网络，只观察精确参数及失败路径，不接触 GitHub 或游戏。
function gh {
    $call=@($args);$global:reffReleaseNotesTest.calls.Add($call);$global:LASTEXITCODE=0
    if ($call[1] -eq 'view') {
        if ($global:reffReleaseNotesTest.failView) { $global:LASTEXITCODE=1;return }
        $assets=@(@{name="REFF-$($global:reffReleaseNotesTest.version).zip"},@{name="REFF-$($global:reffReleaseNotesTest.version).zip.sha256"},@{name='REFF-0.9.0-tools.zip'})
        if ($global:reffReleaseNotesTest.hasTools) { $assets+=@(@{name="REFF-$($global:reffReleaseNotesTest.version)-tools.zip"},@{name="REFF-$($global:reffReleaseNotesTest.version)-tools.zip.sha256"}) }
        return (@{tagName="v$($global:reffReleaseNotesTest.version)";assets=$assets} | ConvertTo-Json -Depth 4 -Compress)
    }
    if ($call[1] -eq 'delete-asset' -and $global:reffReleaseNotesTest.failDelete) { $global:LASTEXITCODE=1 }
}
try {
    $update=Join-Path $PSScriptRoot 'update-release-metadata.ps1'
    & $update -Tag "v$($global:reffReleaseNotesTest.version)"
    $deletes=@($global:reffReleaseNotesTest.calls | Where-Object { $_[1] -eq 'delete-asset' } | ForEach-Object { $_[3] })
    $expected=@("REFF-$($global:reffReleaseNotesTest.version)-tools.zip","REFF-$($global:reffReleaseNotesTest.version)-tools.zip.sha256")
    Assert-Notes (@(Compare-Object $expected $deletes).Count -eq 0) '删除范围不是当前版本的两个工具附件'
    $edits=@($global:reffReleaseNotesTest.calls | Where-Object { $_[1] -eq 'edit' })
    Assert-Notes ($edits.Count -eq 1) '未更新发布说明'
    $noteIndex=[array]::IndexOf($edits[0],'--notes-file')
    $publishedNotes=[IO.File]::ReadAllText($edits[0][$noteIndex+1])
    Assert-Notes ($publishedNotes -eq (Get-REFFReleaseNotes (Join-Path $repoRoot 'CHANGELOG.md') $($global:reffReleaseNotesTest.version))) '发布内容不等于当前版本章节'
    Assert-Notes (@($global:reffReleaseNotesTest.calls | Where-Object { $_[1] -notin @('view','edit','delete-asset') }).Count -eq 0) '元数据更新不应重建或上传核心资产'
    $global:reffReleaseNotesTest.calls.Clear();$global:reffReleaseNotesTest.hasTools=$false
    & $update -Tag "v$($global:reffReleaseNotesTest.version)"
    Assert-Notes ($global:reffReleaseNotesTest.calls.Count -eq 2) '重复维护应只查询和更新说明'
    $global:reffReleaseNotesTest.calls.Clear();$global:reffReleaseNotesTest.failView=$true
    Assert-NotesRejected { & $update -Tag "v$($global:reffReleaseNotesTest.version)" }
    Assert-Notes ($global:reffReleaseNotesTest.calls.Count -eq 1) '查询失败后仍修改了发布'
    $global:reffReleaseNotesTest.calls.Clear();$global:reffReleaseNotesTest.failView=$false;$global:reffReleaseNotesTest.hasTools=$true;$global:reffReleaseNotesTest.failDelete=$true
    Assert-NotesRejected { & $update -Tag "v$($global:reffReleaseNotesTest.version)" }
    Assert-Notes ($global:reffReleaseNotesTest.calls.Count -eq 3) '删除失败未及时终止'
} finally { Remove-Item Function:gh;Remove-Variable reffReleaseNotesTest -Scope Global }
@{passed=$true;currentVersionOnly=$true;exactToolAssetCleanup=$true;coreAssetsUnchanged=$true;failurePaths=$true} |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $fixture 'summary.json')
