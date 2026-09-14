param([switch]$SkipTypeCheck, [switch]$IncludeExamples)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$shellRoot = Join-Path $repoRoot 'web\shell'
$vite = Join-Path $shellRoot 'node_modules\.bin\vite.CMD'
$vueTsc = Join-Path $shellRoot 'node_modules\.bin\vue-tsc.CMD'

if (-not (Test-Path -LiteralPath $vite)) { throw '缺少 Web 构建依赖，请先在 web/shell 安装锁定依赖。' }

# Web 构建顺序固定为公共运行时、Shell、可选示例；正式构建默认不处理示例插件。
if (-not $SkipTypeCheck) {
    & $vueTsc --noEmit --project (Join-Path $shellRoot 'tsconfig.json')
    if ($LASTEXITCODE -ne 0) { throw 'Shell Vue 类型检查失败' }
    if ($IncludeExamples) {
        & $vueTsc --noEmit --project (Join-Path $repoRoot 'web\plugins\tsconfig.json')
        if ($LASTEXITCODE -ne 0) { throw '示例插件类型检查失败' }
    }
}
& $vite build --config (Join-Path $repoRoot 'web\shared\vite.config.ts')
if ($LASTEXITCODE -ne 0) { throw '公共 UI 构建失败' }
Push-Location $shellRoot
try {
    & $vite build
    if ($LASTEXITCODE -ne 0) { throw 'Shell 构建失败' }
} finally {
    Pop-Location
}

if ($IncludeExamples) {
    foreach ($name in @('example.vue', 'example.react', 'example.html')) {
        & $vite build --config (Join-Path $repoRoot "web\plugins\$name\vite.config.ts")
        if ($LASTEXITCODE -ne 0) { throw "示例插件构建失败：$name" }
    }
}

# 产物检查以最终 JS 为准：每个页面必须引用公共模块，业务目录不能再次出现 Element Plus 完整包。
$sharedUrl = 'reff://shell/shared/reff-ui.mjs'
$sharedModule = Join-Path $shellRoot 'dist\shared\reff-ui.mjs'
$sharedStyle = Join-Path $shellRoot 'dist\shared\reff-ui.css'
if (-not (Test-Path -LiteralPath $sharedModule) -or -not (Test-Path -LiteralPath $sharedStyle)) { throw 'Shell 产物缺少公共 UI 资源' }
$consumerRoots = @((Join-Path $shellRoot 'dist\assets'))
if ($IncludeExamples) {
    $consumerRoots += @('example.vue', 'example.react', 'example.html') | ForEach-Object {
        Join-Path $repoRoot "web\plugins\$_\ui\dist\assets"
    }
}
foreach ($consumerRoot in $consumerRoots) {
    $scripts = @(Get-ChildItem -LiteralPath $consumerRoot -Filter '*.js' -File)
    if ($scripts.Count -ne 1) { throw "页面业务脚本数量异常：$consumerRoot" }
    $content = Get-Content -LiteralPath $scripts[0].FullName -Raw
    # 资源目录固定为 web/plugins/<插件>/ui/dist/assets，向上三级才是插件目录。
    $pluginName = Split-Path (Split-Path (Split-Path $consumerRoot -Parent) -Parent) -Leaf
    if ($pluginName -eq 'example.vue' -and -not $content.Contains($sharedUrl)) { throw "Vue 页面未引用公共 UI 模块：$($scripts[0].FullName)" }
    if ($content.Contains('Element Plus v2.10.7')) { throw "页面重复打包了 Element Plus：$($scripts[0].FullName)" }
}
$entryDocuments = @((Join-Path $shellRoot 'dist\index.html'))
if ($IncludeExamples) {
    $entryDocuments += @('example.vue', 'example.react', 'example.html') | ForEach-Object {
        Join-Path $repoRoot "web\plugins\$_\ui\dist\index.html"
    }
}
foreach ($entryDocument in $entryDocuments) {
    if (-not (Get-Content -LiteralPath $entryDocument -Raw).Contains('reff://shell/shared/reff-ui.css')) { throw "页面未引用公共 UI 样式：$entryDocument" }
}
$businessBytes = ($consumerRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -File } | Measure-Object Length -Sum).Sum
$profile = if ($IncludeExamples) { '开发示例' } else { '正式 Runtime' }
Write-Host "REFF Web 资源构建完成（$profile）；公共资源 $((Get-Item $sharedModule).Length + (Get-Item $sharedStyle).Length) 字节，页面业务资源 $businessBytes 字节。"
