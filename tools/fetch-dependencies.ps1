param()
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json

# 只下载锁文件中明确的官方依赖；缓存必须通过 SHA256 后才能解压使用。
function Install-LockedArchive($Name, $Entry, $ArchiveName) {
    $cacheRoot = Join-Path $repoRoot '.cache'
    $destination = Join-Path $repoRoot ".deps\$Name"
    New-Item -ItemType Directory -Force -Path $cacheRoot,$destination | Out-Null
    $archive = Join-Path $cacheRoot $ArchiveName
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Entry.url -OutFile $archive -TimeoutSec 600
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Entry.sha256.ToLowerInvariant()) {
        throw "$Name 校验失败，请检查下载文件；不会解压不匹配的归档。"
    }
    & tar -xf $archive -C $destination --strip-components=1
    if ($LASTEXITCODE -ne 0) { throw "$Name 解压失败" }
    Write-Host "$Name 已按锁定版本准备完成"
}

Install-LockedArchive 'reframework' $lock.reframework 'reframework.tar.gz'
Install-LockedArchive 'cef' $lock.cef 'cef-minimal.tar.bz2'
