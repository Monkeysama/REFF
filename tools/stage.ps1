param(
    [string]$BuildRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build'),
    [switch]$IncludeExamples,
    [string]$StagingRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'staging')
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$StagingRoot = [IO.Path]::GetFullPath($StagingRoot)
$stageRoot = Join-Path $StagingRoot 'reframework'
$runtimeRoot = Join-Path $stageRoot 'reff\runtime'
$cefRoot = Join-Path $repoRoot '.deps\cef'
$cachePath = Join-Path $BuildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { throw "构建目录缺少 CMakeCache：$cachePath" }
$experimentalGames = Select-String -LiteralPath $cachePath -Pattern '^REFF_ENABLE_EXPERIMENTAL_GAMES:BOOL=ON$' -Quiet

# 开发示例包含中文输入回归入口，禁止用未编译透明 IME 代理的默认 DLL 生成游戏测试包。
if ($IncludeExamples) {
    $imeEnabled = Select-String -LiteralPath $cachePath -Pattern '^REFF_ENABLE_IME_PROXY:BOOL=ON$' -Quiet
    if (-not $imeEnabled) { throw '示例游戏构建必须使用 tools\build.ps1 -Profile GameTest，再以 -BuildRoot build-ime 生成 staging。' }
}

# Windows PowerShell 5.1 的 .NET Framework 没有 Path.GetRelativePath；本函数只接受根目录内的规范绝对路径。
function Get-REFFRelativePath([string]$Root, [string]$Path) {
    $normalizedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $normalizedPath = [IO.Path]::GetFullPath($Path)
    if (-not $normalizedPath.StartsWith($normalizedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "路径不在指定根目录内：$normalizedPath"
    }
    return $normalizedPath.Substring($normalizedRoot.Length)
}

# staging 完全由本脚本生成；先清除上一轮产物，保证清单不会累积旧资源。
if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $runtimeRoot,(Join-Path $stageRoot 'plugins'),(Join-Path $stageRoot 'autorun\reff'),(Join-Path $stageRoot 'reff\ui'),(Join-Path $stageRoot 'reff\plugins'),(Join-Path $stageRoot 'reff\licenses') | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $BuildRoot 'Release\REFF.dll'))) { throw "找不到构建产物：$BuildRoot" }
Copy-Item -LiteralPath (Join-Path $BuildRoot 'Release\REFF.dll') -Destination (Join-Path $stageRoot 'plugins\REFF.dll')
Copy-Item -LiteralPath (Join-Path $BuildRoot 'Release\reff-host.dll') -Destination $runtimeRoot
Copy-Item -LiteralPath (Join-Path $cefRoot 'Release\bootstrap.exe') -Destination (Join-Path $runtimeRoot 'reff-host.exe')

# 按 CEF 发行包提供的运行文件复制，排除 import library、开发符号和额外 bootstrap 工具。
Get-ChildItem -LiteralPath (Join-Path $cefRoot 'Release') -File | Where-Object { $_.Extension -notin '.lib','.pdb','.exe' } | Copy-Item -Destination $runtimeRoot
Get-ChildItem -LiteralPath (Join-Path $cefRoot 'Resources') | Copy-Item -Destination $runtimeRoot -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'lua\REFF.lua') -Destination (Join-Path $stageRoot 'autorun\REFF.lua')
Copy-Item -LiteralPath (Join-Path $repoRoot 'lua\reff\bridge.lua') -Destination (Join-Path $stageRoot 'autorun\reff\bridge.lua')
Copy-Item -LiteralPath (Join-Path $repoRoot 'lua\reff\sdk.lua') -Destination (Join-Path $stageRoot 'autorun\reff\sdk.lua')

$shellDist = Join-Path $repoRoot 'web\shell\dist'
if (Test-Path -LiteralPath (Join-Path $shellDist 'index.html')) {
    # 有可复现的离线构建产物时优先使用 Vue Shell；没有 Node 依赖时保留原型作为安全回退。
    Get-ChildItem -LiteralPath $shellDist -File -Recurse | ForEach-Object {
        $relative = Get-REFFRelativePath $shellDist $_.FullName
        $destination = Join-Path $stageRoot ('reff\ui\' + $relative)
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
    }
} else {
    throw '缺少 Vue Shell 构建产物，请先运行 tools\build-web.ps1。'
}

if ($IncludeExamples) {
    # 示例脚本和页面只进入显式开发构建；正式 staging 不包含测试业务或其 manifest。
    Copy-Item -LiteralPath (Join-Path $repoRoot 'examples\lua\REFF.examples.lua') -Destination (Join-Path $stageRoot 'autorun\REFF.examples.lua')
    $pluginRoot = Join-Path $repoRoot 'web\plugins'
    foreach ($pluginDirectory in Get-ChildItem -LiteralPath $pluginRoot -Directory | Where-Object { $_.Name -in @('example.vue', 'example.react', 'example.html') }) {
        $manifestPath = Join-Path $pluginDirectory.FullName 'manifest.json'
        if (-not (Test-Path -LiteralPath $manifestPath)) { continue }
        # Windows PowerShell 5.1 会把无 BOM 的 UTF-8 JSON 按系统 ANSI 解码，必须显式指定 UTF-8。
        $manifestObject = [IO.File]::ReadAllText($manifestPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
        $destinationRoot = Join-Path $stageRoot ('reff\plugins\' + $pluginDirectory.Name)
        New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
        Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $destinationRoot 'manifest.json')
        $entrySource = Join-Path $pluginDirectory.FullName $manifestObject.ui.entry
        if (-not (Test-Path -LiteralPath $entrySource -PathType Leaf)) { throw "示例插件入口不存在：$entrySource" }
        $entryFiles = if ($manifestObject.ui.mode -eq 'component') {
            @(Get-Item -LiteralPath $entrySource)
        } else {
            @(Get-ChildItem -LiteralPath (Split-Path -Parent $entrySource) -File -Recurse)
        }
        $entryFiles | ForEach-Object {
            $relative = Get-REFFRelativePath $pluginDirectory.FullName $_.FullName
            $destination = Join-Path $destinationRoot $relative
            New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        }
    }
}

Copy-Item -LiteralPath (Join-Path $cefRoot 'LICENSE.txt') -Destination (Join-Path $stageRoot 'reff\licenses\CEF.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot '.deps\reframework\LICENSE') -Destination (Join-Path $stageRoot 'reff\licenses\REFramework.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $stageRoot 'reff\licenses\REFF.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'dependencies.lock.json') -Destination (Join-Path $stageRoot 'reff\dependencies.lock.json')
Copy-Item -LiteralPath (Join-Path $repoRoot 'version.json') -Destination (Join-Path $stageRoot 'reff\ui\version.json')

# 按官方要求仅为生成的 Runtime 目录增加沙箱读取权限，保留原有 ACL。
& icacls $runtimeRoot /grant '*S-1-15-2-2:(OI)(CI)(RX)' /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'CEF Runtime 沙箱读取权限设置失败' }

# 文件清单用于限定后续部署范围；配置与缓存不列入发布文件。
$manifest = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse | ForEach-Object {
    [pscustomobject]@{ path = (Get-REFFRelativePath $stageRoot $_.FullName).Replace('\','/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); bytes = $_.Length }
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $StagingRoot 'manifest.json') -Encoding utf8
# 构建能力元数据只供本地部署脚本校验，不进入 reframework 安装目录或发布清单。
[pscustomobject]@{ schemaVersion = 1; experimentalGames = [bool]$experimentalGames; buildRoot = [IO.Path]::GetFullPath($BuildRoot) } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $StagingRoot 'build-profile.json') -Encoding utf8
$profile = if ($IncludeExamples) { '开发示例' } else { '正式 Runtime' }
Write-Host "Staging 已准备：$stageRoot（$profile）"
Write-Host "文件数：$($manifest.Count)，合计字节：$(($manifest | Measure-Object -Property bytes -Sum).Sum)"
