param(
    # 发布标签，例如 v0.1.0-preview7；标签的基础版本必须与 version.json 一致。
    [Parameter(Mandatory = $true)][string]$Tag,
    # 仅在传入此开关时调用 GitHub CLI 创建或更新 Release；默认只生成本地资产。
    [switch]$Publish,
    # 使用已有构建产物时跳过构建步骤，仍会重新生成 staging、压缩包和校验文件。
    [switch]$SkipBuild,
    # 覆盖自动发现的发布说明文件。
    [string]$NotesFile
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$versionPath = Join-Path $repoRoot 'version.json'
$artifactsRoot = Join-Path $repoRoot 'artifacts'

function Invoke-REFFScript([string]$Name, [string[]]$Arguments) {
    # PowerShell 对脚本参数数组的绑定会把 -Profile 当作 ValidateSet 的值；按脚本契约显式转发参数。
    $scriptPath = Join-Path $repoRoot "tools\$Name"
    switch ($Name) {
        'build-web.ps1' {
            if ($Arguments -contains '-IncludeExamples') { & $scriptPath -IncludeExamples }
            else { & $scriptPath }
        }
        'build.ps1' {
            $profileIndex = [Array]::IndexOf($Arguments, '-Profile')
            $profile = if ($profileIndex -ge 0) { $Arguments[$profileIndex + 1] } else { 'Default' }
            if ($Arguments -contains '-SkipTests') { & $scriptPath -Profile $profile -SkipTests }
            else { & $scriptPath -Profile $profile }
        }
        'stage.ps1' {
            $buildIndex = [Array]::IndexOf($Arguments, '-BuildRoot')
            $buildRoot = if ($buildIndex -ge 0) { $Arguments[$buildIndex + 1] } else { Join-Path $repoRoot 'build' }
            if ($Arguments -contains '-IncludeExamples') { & $scriptPath -BuildRoot $buildRoot -IncludeExamples }
            else { & $scriptPath -BuildRoot $buildRoot }
        }
        default { throw "不支持的构建脚本：$Name" }
    }
    if ($LASTEXITCODE -ne 0) { throw "$Name 执行失败（退出码：$LASTEXITCODE）" }
}

# Windows PowerShell 5.1 没有 Path.GetRelativePath；发布源码包只处理仓库根目录内的路径。
function Get-REFFRelativePath([string]$Root, [string]$Path) {
    $normalizedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $normalizedPath = [IO.Path]::GetFullPath($Path)
    if (-not $normalizedPath.StartsWith($normalizedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "路径不在仓库根目录内：$normalizedPath" }
    return $normalizedPath.Substring($normalizedRoot.Length)
}

# 将标签限制为可排序的语义化版本，避免把任意字符串写入文件名或 Release 标签。
$normalizedTag = $Tag.Trim()
if ($normalizedTag.StartsWith('refs/tags/')) { $normalizedTag = $normalizedTag.Substring(10) }
$releaseVersion = $normalizedTag.TrimStart('v')
if ($releaseVersion -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') {
    throw "标签不是有效的语义化版本：$Tag"
}
$versionObject = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
$projectVersion = [string]$versionObject.version
if ($releaseVersion -notmatch "^$([regex]::Escape($projectVersion))(?:-|$)") {
    throw "发布标签 $normalizedTag 与 version.json 的基础版本 $projectVersion 不一致"
}

New-Item -ItemType Directory -Force -Path $artifactsRoot | Out-Null
$runtimeZip = Join-Path $artifactsRoot "REFF-$releaseVersion.zip"
$examplesZip = Join-Path $artifactsRoot "REFF-$releaseVersion-examples.zip"
$examplesStageRoot = Join-Path $repoRoot 'staging\examples\reframework'
$runtimeHash = "$runtimeZip.sha256"
$examplesHash = "$examplesZip.sha256"

if (-not $SkipBuild) {
    # 构建前删除同版本旧资产，防止中途失败后继续使用上一轮压缩包。
    foreach ($oldAsset in @($runtimeZip, $examplesZip, $runtimeHash, $examplesHash)) {
        if (Test-Path -LiteralPath $oldAsset) { Remove-Item -LiteralPath $oldAsset -Force }
    }
    # 示例 Web 资源和 GameTest 原生构建使用透明 IME 代理，先生成开发示例包。
    Invoke-REFFScript 'build-web.ps1' @('-IncludeExamples')
    Invoke-REFFScript 'build.ps1' @('-Profile', 'GameTest')
    Invoke-REFFScript 'stage.ps1' @('-BuildRoot', (Join-Path $repoRoot 'build-ime'), '-IncludeExamples')
    $stageRoot = Join-Path $repoRoot 'staging\reframework'
    # 示例包是正式 Runtime 的增量包，只提取示例后端脚本和三个插件目录，不重复打包 DLL/CEF/Shell。
    if (Test-Path -LiteralPath (Split-Path $examplesStageRoot -Parent)) { Remove-Item -LiteralPath (Split-Path $examplesStageRoot -Parent) -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $examplesStageRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $examplesStageRoot 'autorun'), (Join-Path $examplesStageRoot 'reff\plugins') | Out-Null
    Copy-Item -LiteralPath (Join-Path $stageRoot 'autorun\REFF.examples.lua') -Destination (Join-Path $examplesStageRoot 'autorun\REFF.examples.lua') -Force
    foreach ($exampleId in @('example.vue', 'example.react', 'example.html')) {
        $source = Join-Path $stageRoot "reff\plugins\$exampleId"
        $destination = Join-Path $examplesStageRoot "reff\plugins\$exampleId"
        Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
    }
    # 附带可复用的源码开发目录、公共 SDK/类型源码和锁定依赖；排除已构建 dist，避免重复占用体积。
    $devRoot = Join-Path $examplesStageRoot 'reff\examples-dev'
    New-Item -ItemType Directory -Force -Path $devRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'web\examples-dev\README.zh-CN.md') -Destination $devRoot -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'web\examples-dev\package.json') -Destination $devRoot -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'web\examples-dev\pnpm-workspace.yaml') -Destination $devRoot -Force
    $devFiles = @(
        (Join-Path $repoRoot 'web\plugins\tsconfig.json'),
        (Join-Path $repoRoot 'web\sdk'),
        (Join-Path $repoRoot 'web\shared\src'),
        (Join-Path $repoRoot 'web\shell\package.json'),
        (Join-Path $repoRoot 'web\shell\pnpm-lock.yaml'),
        (Join-Path $repoRoot 'examples\lua\REFF.examples.lua')
    )
    foreach ($source in $devFiles) {
        $relative = Get-REFFRelativePath $repoRoot $source
        $destination = Join-Path $devRoot $relative
        if ((Get-Item -LiteralPath $source).PSIsContainer) {
            New-Item -ItemType Directory -Force -Path $destination | Out-Null
            Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object {
                $childRelative = Get-REFFRelativePath $source $_.FullName
                $childDestination = Join-Path $destination $childRelative
                New-Item -ItemType Directory -Force -Path (Split-Path $childDestination) | Out-Null
                Copy-Item -LiteralPath $_.FullName -Destination $childDestination -Force
            }
        } else {
            New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination -Force
        }
    }
    foreach ($exampleId in @('example.vue', 'example.react', 'example.html')) {
        $source = Join-Path $repoRoot "web\plugins\$exampleId"
        $destination = Join-Path $devRoot "web\plugins\$exampleId"
        Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object { $_.FullName -notmatch '\\ui\\dist\\' } | ForEach-Object {
            $relative = Get-REFFRelativePath $source $_.FullName
            $target = Join-Path $destination $relative
            New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        }
    }
    $syncScript = Join-Path $repoRoot 'web\examples-dev\tools\build-and-sync.ps1'
    New-Item -ItemType Directory -Force -Path (Join-Path $devRoot 'tools') | Out-Null
    Copy-Item -LiteralPath $syncScript -Destination (Join-Path $devRoot 'tools\build-and-sync.ps1') -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'web\examples-dev\tools\watch-and-sync.ps1') -Destination (Join-Path $devRoot 'tools\watch-and-sync.ps1') -Force
    Compress-Archive -Path $examplesStageRoot -DestinationPath $examplesZip -CompressionLevel Optimal

    # 正式包重新构建默认原生配置，并且不带测试脚本和示例插件。
    Invoke-REFFScript 'build-web.ps1' @()
    Invoke-REFFScript 'build.ps1' @('-Profile', 'Default')
    Invoke-REFFScript 'stage.ps1' @('-BuildRoot', (Join-Path $repoRoot 'build'))
    Compress-Archive -Path $stageRoot -DestinationPath $runtimeZip -CompressionLevel Optimal
}

function Assert-ReleaseArchive([string]$Archive, [bool]$Examples) {
    # 压缩包必须只有 reframework 顶层目录，避免用户解压后得到错误的嵌套路径。
    $entries = @(& tar -tf $Archive 2>$null)
    if ($LASTEXITCODE -ne 0 -or $entries.Count -eq 0) { throw "无法读取发布压缩包：$Archive" }
    foreach ($entry in $entries) {
        $normalized = ([string]$entry).TrimStart('./').Replace('\', '/')
        if ($normalized -and -not $normalized.StartsWith('reframework/')) { throw "压缩包缺少 reframework 顶层目录：$Archive" }
    }
    $required = if ($Examples) {
        @(
            'reframework/autorun/REFF.examples.lua',
            'reframework/reff/plugins/example.vue/manifest.json',
            'reframework/reff/plugins/example.react/manifest.json',
            'reframework/reff/plugins/example.html/manifest.json',
            'reframework/reff/examples-dev/README.zh-CN.md',
            'reframework/reff/examples-dev/web/shell/package.json',
            'reframework/reff/examples-dev/web/shell/pnpm-lock.yaml'
        )
    } else { @('reframework/plugins/REFF.dll') }
    foreach ($path in $required) {
        if (-not ($entries | Where-Object { ([string]$_).TrimStart('./').Replace('\', '/') -eq $path })) {
            throw "压缩包缺少关键文件 $path：$Archive"
        }
    }
    if (-not $Examples) {
        if ($entries | Where-Object { ([string]$_).TrimStart('./').Replace('\', '/') -eq 'reframework/autorun/REFF.examples.lua' }) {
            throw "正式 Runtime 包不得包含示例脚本：$Archive"
        }
        if ($entries | Where-Object { ([string]$_).TrimStart('./').Replace('\', '/') -match '^reframework/reff/plugins/example\.' }) {
            throw "正式 Runtime 包不得包含示例插件：$Archive"
        }
    } else {
        # 增量示例包不得携带 Runtime 文件；用户应先安装同版本正式包再合并此包。
        $allowed = @(
            'reframework/autorun/REFF.examples.lua',
            'reframework/reff/plugins/example.vue/',
            'reframework/reff/plugins/example.react/',
            'reframework/reff/plugins/example.html/',
            'reframework/reff/examples-dev/'
        )
        foreach ($entry in $entries) {
            $normalized = ([string]$entry).TrimStart('./').Replace('\', '/')
            if (-not $normalized -or $normalized -eq 'reframework/' -or $normalized.EndsWith('/')) { continue }
            if (-not ($normalized -eq 'reframework/autorun/REFF.examples.lua' -or $normalized -match '^reframework/reff/plugins/example\.(vue|react|html)/' -or $normalized -match '^reframework/reff/examples-dev/')) {
                throw "示例增量包包含非插件文件 $normalized：$Archive"
            }
        }
    }
}

foreach ($asset in @($runtimeZip, $examplesZip)) {
    if (-not (Test-Path -LiteralPath $asset -PathType Leaf)) { throw "缺少发布资产：$asset" }
    Assert-ReleaseArchive $asset ($asset -eq $examplesZip)
    $hash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($asset))" | Set-Content -LiteralPath "$asset.sha256" -Encoding ascii
}

$notesPath = $NotesFile
if (-not $notesPath) {
    $candidate = Join-Path $repoRoot "docs\release-notes-$releaseVersion.zh-CN.md"
    if (Test-Path -LiteralPath $candidate) { $notesPath = $candidate }
}
if (-not $notesPath) {
    $notesPath = Join-Path $artifactsRoot "release-notes-$releaseVersion.zh-CN.md"
    @("# REFramework Frontend $releaseVersion", '', '本版本提供 REFF 正式 Runtime 包及可叠加安装的示例插件增量包。', '', '支持范围：Monster Hunter Wilds、Windows x64、DirectX 12、键鼠。') |
        Set-Content -LiteralPath $notesPath -Encoding utf8
}
if (-not (Test-Path -LiteralPath $notesPath -PathType Leaf)) { throw "发布说明不存在：$notesPath" }

Write-Host "发布资产已生成："
Get-Item -LiteralPath $runtimeZip, $examplesZip, $runtimeHash, $examplesHash | Select-Object Name, Length | Format-Table -AutoSize

if ($Publish) {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw '未找到 GitHub CLI gh；请先安装并执行 gh auth login。' }
    $isPrerelease = $releaseVersion -match '-(alpha|beta|rc|preview)(?:[.-]\d+|\d+|$)'
    & gh release view $normalizedTag --json tagName *> $null
    $releaseExists = ($LASTEXITCODE -eq 0)
    if ($releaseExists) {
        # 已存在的标签采用覆盖上传，允许修复构建后重复发布同一版本。
        & gh release edit $normalizedTag --title "REFramework Frontend $releaseVersion" --notes-file $notesPath
        if ($LASTEXITCODE -ne 0) { throw "GitHub Release 说明更新失败（退出码：$LASTEXITCODE）" }
        & gh release upload $normalizedTag $runtimeZip $examplesZip $runtimeHash $examplesHash --clobber
        if ($LASTEXITCODE -ne 0) { throw "GitHub Release 资产更新失败（退出码：$LASTEXITCODE）" }
    }
    else {
        $ghArguments = @('release', 'create', $normalizedTag, $runtimeZip, $examplesZip, $runtimeHash, $examplesHash, '--title', "REFramework Frontend $releaseVersion", '--notes-file', $notesPath, '--verify-tag')
        if ($isPrerelease) { $ghArguments += '--prerelease' }
        & gh @ghArguments
        if ($LASTEXITCODE -ne 0) { throw "GitHub Release 发布失败（退出码：$LASTEXITCODE）" }
    }
    Write-Host "GitHub Release 已发布：$normalizedTag"
}
else {
    Write-Host '未上传 GitHub Release；需要发布时追加 -Publish。'
}
