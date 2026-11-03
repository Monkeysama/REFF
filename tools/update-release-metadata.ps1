param([Parameter(Mandatory=$true)][string]$Tag)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$version=[string](Get-Content -LiteralPath (Join-Path $repoRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
$normalizedTag=$Tag.Trim() -replace '^refs/tags/',''
if ($normalizedTag -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$' -or $normalizedTag -ne ('v'+$version)) { throw '发布说明标签必须与 version.json 一致' }
. (Join-Path $PSScriptRoot 'release-notes.ps1')
$notes=Get-REFFReleaseNotes (Join-Path $repoRoot 'CHANGELOG.md') $version
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw '未找到 GitHub CLI gh' }
# 仅维护已发布版本的文字与废弃附件，不重建二进制、不移动标签、不修改核心附件。
$raw=& gh release view $normalizedTag --json tagName,assets
if ($LASTEXITCODE -ne 0) { throw 'GitHub Release 查询失败' }
$release=$raw | ConvertFrom-Json
if ($release.tagName -ne $normalizedTag) { throw 'GitHub Release 标签不符' }
$artifacts=Join-Path $repoRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$notesFile=Join-Path $artifacts "REFF-$version-notes.md"
[IO.File]::WriteAllText($notesFile,$notes,(New-Object Text.UTF8Encoding($false)))
& gh release edit $normalizedTag --title "REFF $normalizedTag" --notes-file $notesFile --prerelease=false --latest
if ($LASTEXITCODE -ne 0) { throw 'GitHub Release 更新失败' }
# 只删除当前版本的两个工具附件；其他版本及核心附件保持原样。
foreach ($name in @("REFF-$version-tools.zip","REFF-$version-tools.zip.sha256")) {
    if (@($release.assets.name) -contains $name) {
        & gh release delete-asset $normalizedTag $name --yes
        if ($LASTEXITCODE -ne 0) { throw "GitHub Release 旧工具资产删除失败：$name" }
    }
}
Write-Host "GitHub Release 说明与附件已更新：$normalizedTag"
