$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$lock = (Get-Content -LiteralPath (Join-Path $repoRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json).webview2
$sdkRoot = Join-Path $repoRoot '.deps\webview2'
$archive = Join-Path $repoRoot ".cache\microsoft.web.webview2.$($lock.version).nupkg"

# 只准备锁定官方 SDK；不安装系统 Runtime，也不触碰游戏或现有构建产物。
New-Item -ItemType Directory -Force -Path (Split-Path $archive -Parent),$sdkRoot | Out-Null
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $lock.url -OutFile $archive }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $lock.sha256) { throw 'WebView2 SDK SHA256 校验失败' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
# 校验后覆盖 SDK 文件，避免旧版本头文件和库被误用。
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        $target = [IO.Path]::GetFullPath((Join-Path $sdkRoot $entry.FullName))
        $prefix = [IO.Path]::GetFullPath($sdkRoot).TrimEnd('\') + '\'
        if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'SDK 归档路径越界' }
        if (-not $entry.Name) { continue }
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
    }
} finally { $zip.Dispose() }
