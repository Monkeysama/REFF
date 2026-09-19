param(
    [Parameter(Mandatory = $true)][ValidateSet('example.vue', 'example.react', 'example.html')][string]$PluginId,
    [Parameter(Mandatory = $true)][string]$GameReframeworkRoot,
    [int]$PollMilliseconds = 500,
    [switch]$Once
)

$ErrorActionPreference = 'Stop'

# 开发监视器只调用网页构建与插件目录同步，不启动、关闭或注入游戏；构建成功后由示例页的 ui.reload 自动刷新。
$devRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$syncScript = Join-Path $PSScriptRoot 'build-and-sync.ps1'
$pluginRoot = Join-Path $devRoot "web\plugins\$PluginId"
if (-not (Test-Path -LiteralPath $syncScript -PathType Leaf)) { throw "Missing sync script: $syncScript" }
if ($PollMilliseconds -lt 100) { throw 'PollMilliseconds must be at least 100.' }

# 监视源码、manifest、构建配置和共享源码；排除 ui/dist，避免同步产物触发自身循环。
function Get-InputSnapshot {
    $snapshot = @{}
    $roots = @((Join-Path $pluginRoot 'src'), (Join-Path $pluginRoot 'ui\index.html'), (Join-Path $pluginRoot 'manifest.json'), (Join-Path $pluginRoot 'vite.config.ts'), (Join-Path $devRoot 'web\sdk'), (Join-Path $devRoot 'web\shared\src'))
    foreach ($root in $roots) {
        if (Test-Path -LiteralPath $root -PathType Leaf) {
            $files = @(Get-Item -LiteralPath $root)
        } elseif (Test-Path -LiteralPath $root -PathType Container) {
            $files = @(Get-ChildItem -LiteralPath $root -File -Recurse)
        } else { $files = @() }
        foreach ($file in $files) {
            $stamp = [string]$file.LastWriteTimeUtc.Ticks + ':' + [string]$file.Length
            $snapshot.Add([string]$file.FullName, $stamp)
        }
    }
    return $snapshot
}

function Invoke-Sync {
    & powershell.exe -ExecutionPolicy Bypass -File $syncScript -PluginId $PluginId -GameReframeworkRoot $GameReframeworkRoot
    if ($LASTEXITCODE -ne 0) { throw "Build or sync failed: $PluginId" }
}

Invoke-Sync
if ($Once) { exit 0 }
$lastSnapshot = Get-InputSnapshot
Write-Host "Watching $PluginId. Press Ctrl+C to stop."
while ($true) {
    Start-Sleep -Milliseconds $PollMilliseconds
    $currentSnapshot = Get-InputSnapshot
    $changed = $currentSnapshot.Count -ne $lastSnapshot.Count
    if (-not $changed) {
        foreach ($path in $currentSnapshot.Keys) {
            if (-not $lastSnapshot.ContainsKey($path) -or $lastSnapshot[$path] -ne $currentSnapshot[$path]) { $changed = $true; break }
        }
    }
    if (-not $changed) { continue }

    # 防抖：编辑器保存可能连续修改多个文件，等待一小段时间后只构建一次。
    Start-Sleep -Milliseconds 300
    $lastSnapshot = Get-InputSnapshot
    try { Invoke-Sync; Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Synced $PluginId; waiting for automatic page reload." }
    catch { Write-Warning $_.Exception.Message }
}
