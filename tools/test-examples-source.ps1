param([switch]$Offline)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $repoRoot ('artifacts/webview2-p4/examples-source-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$dev=Join-Path $fixture 'source'
New-Item -ItemType Directory -Path $dev -Force | Out-Null
# 只复制必要源码，隔离 Shell 的依赖目录，证明三个示例可独立构建。
foreach ($relative in @('web/plugins','web/sdk','web/shared/src','web/examples-dev/tools')) {
    $source=Join-Path $repoRoot $relative
    $destination=if ($relative -eq 'web/examples-dev/tools') { Join-Path $dev 'tools' } else { Join-Path $dev $relative }
    foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object { $_.FullName -notmatch '\\(node_modules|dist|\.git)\\' }) {
        $path=Join-Path $destination $file.FullName.Substring($source.Length+1)
        New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $path -Force
    }
}
$target=Join-Path $fixture 'sync-target/reframework'
New-Item -ItemType Directory -Path (Join-Path $target 'reff/plugins/thirdparty') -Force | Out-Null
$sentinel=Join-Path $target 'reff/plugins/thirdparty/keep.txt'
[IO.File]::WriteAllText($sentinel,'preserve third-party')
$results=@()
# 在源码夹具中逐个安装各自锁定依赖；不提供 Shell/node_modules，证明插件项目实际独立可构建。
foreach ($kind in @('vue','react','html')) {
    $plugin=Join-Path $dev ('web/plugins/example.'+$kind)
    Push-Location $plugin
    try {
        $args=@('install','--frozen-lockfile');if ($Offline) { $args+='--offline' }
        & pnpm @args
        if ($LASTEXITCODE -ne 0) { throw "独立示例依赖安装失败：$kind" }
        & pnpm run typecheck
        if ($LASTEXITCODE -ne 0) { throw "独立示例类型检查失败：$kind" }
        & pnpm run build
        if ($LASTEXITCODE -ne 0) { throw "独立示例构建失败：$kind" }
    } finally { Pop-Location }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $dev 'tools/watch-and-sync.ps1') -PluginId ('example.'+$kind) -GameReframeworkRoot $target -Once
    if ($LASTEXITCODE -ne 0) { throw "源码监视同步入口失败：$kind" }
    if (-not (Test-Path -LiteralPath (Join-Path $target ("reff/plugins/example.$kind/.reff-dev-version")))) { throw '未生成开发刷新标记' }
    if ([IO.File]::ReadAllText($sentinel) -ne 'preserve third-party') { throw '同步修改了第三方文件' }
    $results+=@{framework=$kind;lockedInstall=$true;typecheck=$true;build=$true;watchOnceSync=$true}
}
@{passed=$true;frameworks=$results;shellDependenciesInstalled=$false;gameValidated=$false;fixture=$fixture} |
    ConvertTo-Json -Depth 6 | Tee-Object -FilePath (Join-Path $fixture 'summary.json')
