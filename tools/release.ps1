param(
    [Parameter(Mandatory=$true)][string]$Tag,
    [switch]$Publish,
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
# 正式版标签必须与唯一版本源完全一致，不接受预览后缀或任意文件名。
$normalizedTag=$Tag.Trim() -replace '^refs/tags/',''
$projectVersion=[string](Get-Content -LiteralPath (Join-Path $repoRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
if ($normalizedTag -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$' -or $normalizedTag -ne ('v'+$projectVersion)) {
    throw "正式发布标签必须为 v$projectVersion：$Tag"
}
# 发布必须来自标签所指提交，防止手动工作流将其他分支产物挂到正式标签。
if ($Publish) {
    $tagCommit=& git -C $repoRoot rev-parse --verify "$normalizedTag^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) { throw '正式发布标签尚不存在' }
    $headCommit=& git -C $repoRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $tagCommit -ne $headCommit) { throw '当前提交与发布标签不符' }
}
$artifactsRoot=Join-Path $repoRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifactsRoot | Out-Null
$runtimeZip=Join-Path $artifactsRoot "REFF-$projectVersion.zip"
$runtimeHash="$runtimeZip.sha256"
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-web.ps1')
    & (Join-Path $PSScriptRoot 'build.ps1')
}
# 正式发布只打包核心运行时；示例保留源码，开发者可自行构建。
& (Join-Path $PSScriptRoot 'stage.ps1') -BuildRoot (Join-Path $repoRoot 'build-webview2-release')
Compress-Archive -Path (Join-Path $repoRoot 'staging/reframework') -DestinationPath $runtimeZip -CompressionLevel Optimal -Force
$hash=(Get-FileHash -LiteralPath $runtimeZip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($runtimeZip))" | Set-Content -LiteralPath $runtimeHash -Encoding ascii
& (Join-Path $PSScriptRoot 'verify-release.ps1') -Archives @($runtimeZip)
Get-Item -LiteralPath $runtimeZip,$runtimeHash | Select-Object Name,Length | Format-Table -AutoSize
if ($Publish) {
    # 上传由显式参数或受信任标签工作流授权；游戏测试状态独立记录。
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw '未找到 GitHub CLI gh' }
    & gh release view $normalizedTag --json tagName *> $null
    if ($LASTEXITCODE -eq 0) {
        & gh release edit $normalizedTag --title "REFF $normalizedTag" --notes-file (Join-Path $repoRoot 'CHANGELOG.md') --prerelease=false --latest
        if ($LASTEXITCODE -ne 0) { throw 'GitHub Release 更新失败' }
        & gh release upload $normalizedTag $runtimeZip $runtimeHash --clobber
    } else {
        & gh release create $normalizedTag $runtimeZip $runtimeHash --title "REFF $normalizedTag" --notes-file (Join-Path $repoRoot 'CHANGELOG.md') --verify-tag --latest
    }
    if ($LASTEXITCODE -ne 0) { throw 'GitHub Release 发布失败' }
    Write-Host "GitHub 正式版已发布：$normalizedTag"
} else { Write-Host '本地正式版资产已生成；未上传 GitHub。' }
