param(
    [ValidateSet('Sample', 'Mark', 'Report')][string]$Mode = 'Sample',
    [string]$OutputDirectory = '',
    [ValidateRange(1, 3600)][int]$DurationSeconds = 600,
    [ValidateRange(1, 60)][int]$IntervalSeconds = 2,
    [switch]$IncludeGpu,
    [string]$Phase = '',
    [string]$ReframeworkRoot = 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework'
)
# 本脚本保存为 UTF-8 BOM，兼容 Windows PowerShell 5.1；JSONL 显式按 UTF-8 读写。
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts\resource-observation' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$sampleFile = Join-Path $OutputDirectory 'samples.jsonl'
$markerFile = Join-Path $OutputDirectory 'markers.jsonl'

# 独立追加 JSONL，不读取输入、不启动/退出进程；标记由人工按测试阶段写入。
function Write-Record($Path, $Record) {
    [IO.File]::AppendAllText($Path, (($Record | ConvertTo-Json -Depth 8 -Compress) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
}
if ($Mode -eq 'Mark') {
    if (-not $Phase.Trim()) { throw 'Mark 模式必须提供 -Phase。' }
    Write-Record $markerFile @{ timeUtc = [DateTime]::UtcNow.ToString('o'); phase = $Phase }
    Write-Output "已标记：$Phase"
    return
}

# 汇总只提供观测极值与首尾，不把缓存预热、进程重建或共享工作集相加误报成泄漏。
if ($Mode -eq 'Report') {
    if (-not (Test-Path -LiteralPath $sampleFile)) { throw '没有采样记录。' }
    $samples = @(Get-Content -LiteralPath $sampleFile -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json })
    $rows = @($samples | ForEach-Object { $_.processes })
    $summary = @($rows | Group-Object identity | ForEach-Object {
        $group = @($_.Group)
        $gpuGroup = @($group | Where-Object { $_.PSObject.Properties.Name -contains 'gpuDedicatedBytes' })
        [pscustomobject]@{
            identity = $_.Name; name = $group[0].name; role = $group[0].role; samples = $group.Count
            firstPrivateBytes = $group[0].privateBytes; lastPrivateBytes = $group[-1].privateBytes
            peakPrivateBytes = ($group | Measure-Object privateBytes -Maximum).Maximum
            firstHandles = $group[0].handles; lastHandles = $group[-1].handles
            peakHandles = ($group | Measure-Object handles -Maximum).Maximum
            averageGpuDedicatedBytes = if ($gpuGroup.Count) { ($gpuGroup | Measure-Object gpuDedicatedBytes -Average).Average } else { $null }
            peakGpuDedicatedBytes = if ($gpuGroup.Count) { ($gpuGroup | Measure-Object gpuDedicatedBytes -Maximum).Maximum } else { $null }
            averageGpuSharedBytes = if ($gpuGroup.Count) { ($gpuGroup | Measure-Object gpuSharedBytes -Average).Average } else { $null }
            peakGpuSharedBytes = if ($gpuGroup.Count) { ($gpuGroup | Measure-Object gpuSharedBytes -Maximum).Maximum } else { $null }
            averageGpuEnginePercentSum = if ($gpuGroup.Count) { ($gpuGroup | Measure-Object gpuEnginePercentSum -Average).Average } else { $null }
            peakGpuEnginePercentSum = if ($gpuGroup.Count) { ($gpuGroup | Measure-Object gpuEnginePercentSum -Maximum).Maximum } else { $null }
        }
    })
    $report = @{ sampleCount = $samples.Count; processes = $summary; gameSamples = @($rows | Where-Object role -eq 'game').Count
        markers = @(); notes = 'CPU 为单核 100% 标度。workingSet 含共享页，不可简单相加。GPU engine 是各引擎利用率之和，可超过 100%，不是整卡利用率。首尾增长不等于泄漏。' }
    if (Test-Path -LiteralPath $markerFile) { $report.markers = @(Get-Content -LiteralPath $markerFile -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json }) }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
    Write-Output "已汇总 $($samples.Count) 次采样、$($summary.Count) 个进程实例：$OutputDirectory\summary.json"
    return
}

# PID 与启动时间共同识别进程，避免重启或 PID 复用产生错误 CPU 差分。
# 仅观察指定游戏路径及 REFF Runtime 内的进程；无游戏运行时保留空样本。
$runtimeDirectory = [IO.Path]::GetFullPath((Join-Path $ReframeworkRoot 'reff\runtime')).TrimEnd('\') + '\'
$gameExecutable = [IO.Path]::GetFullPath((Join-Path (Split-Path $ReframeworkRoot -Parent) 'MonsterHunterWilds.exe'))
$previous = @{}
$runId = [Guid]::NewGuid().ToString()
$watch = [Diagnostics.Stopwatch]::StartNew()
Write-Output "只读采样已开始：$OutputDirectory；可用另一终端 Mark，Ctrl+C 可停止。"
while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
    $iterationWatch = [Diagnostics.Stopwatch]::StartNew()
    $now = [DateTime]::UtcNow
    $rows = @()
    $errors = @()
    $candidates = @(Get-CimInstance Win32_Process -Filter "Name = 'MonsterHunterWilds.exe' OR Name = 'reff-host.exe'")
    foreach ($candidate in $candidates) {
        $path = [string]$candidate.ExecutablePath
        if (-not $path) { $errors += "无法读取进程路径：$($candidate.ProcessId)"; continue }
        $role = if ($path.Equals($gameExecutable, [StringComparison]::OrdinalIgnoreCase)) { 'game' }
            elseif ($path.StartsWith($runtimeDirectory, [StringComparison]::OrdinalIgnoreCase)) { 'reff' } else { continue }
        $process = $null
        try {
            $process = Get-Process -Id $candidate.ProcessId -ErrorAction Stop
            $identity = "$($candidate.ProcessId):$($process.StartTime.ToUniversalTime().Ticks)"
            $cpuSeconds = $process.TotalProcessorTime.TotalSeconds
            $cpuPercent = $null
            if ($previous.ContainsKey($identity)) {
                $elapsed = ($now - $previous[$identity].time).TotalSeconds
                if ($elapsed -gt 0) { $cpuPercent = [Math]::Round(100 * ($cpuSeconds - $previous[$identity].cpu) / $elapsed, 2) }
            }
            $previous[$identity] = @{ time = $now; cpu = $cpuSeconds }
            $rows += [pscustomobject]@{ identity = $identity; pid = $candidate.ProcessId; parentPid = $candidate.ParentProcessId
                name = $candidate.Name; role = $role; privateBytes = $process.PrivateMemorySize64; workingSetBytes = $process.WorkingSet64
                handles = $process.HandleCount; threads = $process.Threads.Count; cpuPercentOneCore = $cpuPercent
                gpuDedicatedBytes = if ($IncludeGpu) { 0 } else { $null }; gpuSharedBytes = if ($IncludeGpu) { 0 } else { $null }
                gpuEnginePercentSum = if ($IncludeGpu) { 0.0 } else { $null } }
        } catch { $errors += "进程退出或读取失败：$($candidate.ProcessId)：$($_.Exception.Message)" }
        finally { if ($process) { $process.Dispose() } }
    }
    # 可选 GPU 查询使用 Windows 性能计数器，按 PID 归并各适配器与引擎；查询失败保留进程数据并记录错误。
    if ($IncludeGpu -and $rows.Count) {
        try {
            $gpuSamples = @(Get-Counter -Counter @('\GPU Process Memory(*)\Dedicated Usage',
                '\GPU Process Memory(*)\Shared Usage', '\GPU Engine(*)\Utilization Percentage') -MaxSamples 1 -ErrorAction Stop | Select-Object -ExpandProperty CounterSamples)
            foreach ($gpuSample in $gpuSamples) {
                if ($gpuSample.InstanceName -notmatch '^pid_(\d+)_') { continue }
                $processId = [int]$Matches[1]
                $row = @($rows | Where-Object pid -eq $processId | Select-Object -First 1)
                if (-not $row.Count) { continue }
                $path = $gpuSample.Path.ToLowerInvariant()
                if ($path.EndsWith('\dedicated usage')) { $row[0].gpuDedicatedBytes += [int64]$gpuSample.CookedValue }
                elseif ($path.EndsWith('\shared usage')) { $row[0].gpuSharedBytes += [int64]$gpuSample.CookedValue }
                elseif ($path.EndsWith('\utilization percentage')) { $row[0].gpuEnginePercentSum += [double]$gpuSample.CookedValue }
            }
        } catch {
            $errors += "GPU 性能计数器读取失败：$($_.Exception.Message)"
            foreach ($row in $rows) { $row.gpuDedicatedBytes = $row.gpuSharedBytes = $row.gpuEnginePercentSum = $null }
        }
    }
    $live = @($rows | ForEach-Object identity)
    foreach ($key in @($previous.Keys)) { if ($key -notin $live) { $previous.Remove($key) } }
    Write-Record $sampleFile @{ runId = $runId; timeUtc = $now.ToString('o'); processes = $rows; errors = $errors }
    $remaining = $DurationSeconds - $watch.Elapsed.TotalSeconds
    $untilNextSample = $IntervalSeconds - $iterationWatch.Elapsed.TotalSeconds
    if ($remaining -gt 0 -and $untilNextSample -gt 0) { Start-Sleep -Milliseconds ([int](1000 * [Math]::Min($untilNextSample, $remaining))) }
}
Write-Output '采样完成，可运行 -Mode Report 汇总。'
