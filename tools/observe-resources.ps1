param(
    [ValidateSet('Sample', 'Mark', 'Report')][string]$Mode = 'Sample',
    [string]$OutputDirectory = '',
    [ValidateRange(1, 3600)][int]$DurationSeconds = 600,
    [ValidateRange(1, 60)][int]$IntervalSeconds = 2,
    [switch]$IncludeGpu,
    [string]$Phase = '',
    [string]$RuntimeDirectory = '',
    [string]$ReframeworkRoot = 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework'
)
# 本脚本保存为 UTF-8 BOM，兼容 Windows PowerShell 5.1；JSONL 显式按 UTF-8 读写。
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'game-catalog.ps1')
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

# 汇总私有内存与 CPU 的进程树分位值；工作集不求和，标记按时间归属到阶段。
function Get-Percentile($Values, [double]$Fraction) {
    $sorted = @($Values | Where-Object { $null -ne $_ } | Sort-Object)
    if (-not $sorted.Count) { return $null }
    return $sorted[[int][Math]::Floor(($sorted.Count - 1) * $Fraction)]
}
if ($Mode -eq 'Report') {
    if (-not (Test-Path -LiteralPath $sampleFile)) { throw '没有采样记录。' }
    $samples = @(Get-Content -LiteralPath $sampleFile -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json })
    $rows = @($samples | ForEach-Object { $_.processes })
    $summary = @($rows | Group-Object identity | ForEach-Object {
        $group = @($_.Group)
        $gpuGroup = @($group | Where-Object { $null -ne $_.gpuDedicatedBytes })
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
            averageGpuEnginePercentSum = if (@($group | Where-Object { $null -ne $_.gpuEnginePercentSum }).Count) { ($group | Measure-Object gpuEnginePercentSum -Average).Average } else { $null }
            peakGpuEnginePercentSum = if (@($group | Where-Object { $null -ne $_.gpuEnginePercentSum }).Count) { ($group | Measure-Object gpuEnginePercentSum -Maximum).Maximum } else { $null }
        }
    })
    $report = @{ sampleCount = $samples.Count; processes = $summary; gameSamples = @($rows | Where-Object role -eq 'game').Count
        samplingWallMsP50 = Get-Percentile $samples.samplingWallMs 0.50; samplingWallMsP95 = Get-Percentile $samples.samplingWallMs 0.95
        markers = @(); notes = 'CPU 为单核 100% 标度。workingSet 含共享页，不可简单相加。GPU engine 是各引擎利用率之和，可超过 100%，不是整卡利用率。首尾增长不等于泄漏。' }
    if (Test-Path -LiteralPath $markerFile) { $report.markers = @(Get-Content -LiteralPath $markerFile -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json }) }
    $totals = @($samples | ForEach-Object {
        $sample = $_
        $phaseName = 'unmarked'
        $latest = @($report.markers | Where-Object { [DateTime]$_.timeUtc -le [DateTime]$sample.timeUtc } | Sort-Object timeUtc | Select-Object -Last 1)
        if ($latest.Count) { $phaseName = $latest[0].phase }
        foreach ($role in @('game','reff')) {
            $members = @($sample.processes | Where-Object role -eq $role)
            # 空进程树不算零占用；首次 CPU 差分未知时该样本不参与 CPU 分位值。
            [pscustomobject]@{ runId = $sample.runId; phase = $phaseName; role = $role; count = $members.Count
                privateBytes = if ($members.Count) { ($members | Measure-Object privateBytes -Sum).Sum } else { $null }
                cpuPercentOneCore = if ($members.Count -and -not @($members | Where-Object { $null -eq $_.cpuPercentOneCore }).Count) { ($members | Measure-Object cpuPercentOneCore -Sum).Sum } else { $null }
                gpuDedicatedBytes = if ($members.Count -and -not @($members | Where-Object { $null -eq $_.gpuDedicatedBytes }).Count) { ($members | Measure-Object gpuDedicatedBytes -Sum).Sum } else { $null }
                gpuSharedBytes = if ($members.Count -and -not @($members | Where-Object { $null -eq $_.gpuSharedBytes }).Count) { ($members | Measure-Object gpuSharedBytes -Sum).Sum } else { $null }
            }
        }
    })
    $report.phases = @($totals | Group-Object runId,phase,role | ForEach-Object {
        $group = @($_.Group)
        [pscustomobject]@{ runId = $group[0].runId; phase = $group[0].phase; role = $group[0].role; samples = $group.Count
            presentSamples = @($group | Where-Object count -gt 0).Count
            privateBytesP50 = Get-Percentile $group.privateBytes 0.50; privateBytesP95 = Get-Percentile $group.privateBytes 0.95
            cpuPercentOneCoreP50 = Get-Percentile $group.cpuPercentOneCore 0.50; cpuPercentOneCoreP95 = Get-Percentile $group.cpuPercentOneCore 0.95
            gpuDedicatedBytesP50 = Get-Percentile $group.gpuDedicatedBytes 0.50; gpuSharedBytesP50 = Get-Percentile $group.gpuSharedBytes 0.50
        }
    })
    $report.samplingErrors = @($samples | ForEach-Object { $_.errors } | Select-Object -Unique)
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
    Write-Output "已汇总 $($samples.Count) 次采样、$($summary.Count) 个进程实例：$OutputDirectory\summary.json"
    return
}

# PID 与启动时间共同识别进程，避免重启或 PID 复用产生错误 CPU 差分。
# 目标进程从 reframework 父目录的已支持游戏识别；无游戏运行时保留空样本。
if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path $ReframeworkRoot 'reff\runtime' }
$runtimePrefix = [IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\') + '\'
$gameRoot = Split-Path $ReframeworkRoot -Parent
$gameEntry = Find-REFFGame $repoRoot $gameRoot @('verified', 'experimental')
if (-not $gameEntry) { throw '无法从目标 reframework 目录识别已支持游戏。' }
$gameFile = $gameEntry.executable
$gameExecutable = $gameEntry.executablePath
$gameProcessName = $gameEntry.processName
$previous = @{}
$runId = [Guid]::NewGuid().ToString()
$watch = [Diagnostics.Stopwatch]::StartNew()
Write-Output "只读采样已开始：$OutputDirectory；可用另一终端 Mark，Ctrl+C 可停止。"
while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
    $iterationWatch = [Diagnostics.Stopwatch]::StartNew()
    $now = [DateTime]::UtcNow
    $rows = @()
    $errors = @()
    $inventory = @(Get-CimInstance Win32_Process)
    $selected = @{}
    foreach ($candidate in $inventory) {
        $path = [string]$candidate.ExecutablePath
        if ($path.Equals($gameExecutable, [StringComparison]::OrdinalIgnoreCase)) { $selected[[int]$candidate.ProcessId] = 'game' }
        elseif ($path -and $candidate.Name -eq 'reff-webview2-host.exe' -and
            $path.Equals(($runtimePrefix + $candidate.Name), [StringComparison]::OrdinalIgnoreCase)) { $selected[[int]$candidate.ProcessId] = 'reff' }
    }
    # WebView2 Runtime 在系统目录中；只沿已校验宿主的后代扩展，启动时间约束排除旧父 PID 复用。
    $byId = @{}
    foreach ($candidate in $inventory) { $byId[[int]$candidate.ProcessId] = $candidate }
    do {
        $added = $false
        foreach ($candidate in $inventory) {
            $id = [int]$candidate.ProcessId; $parentId = [int]$candidate.ParentProcessId
            if ($selected.ContainsKey($id) -or $selected[$parentId] -ne 'reff' -or -not $byId.ContainsKey($parentId)) { continue }
            if ($candidate.CreationDate -lt $byId[$parentId].CreationDate) { continue }
            $selected[$id] = 'reff'; $added = $true
        }
    } while ($added)
    $candidates = @($inventory | Where-Object { $selected.ContainsKey([int]$_.ProcessId) })
    foreach ($candidate in $candidates) {
        $path = [string]$candidate.ExecutablePath
        if (-not $path) { $errors += "无法读取进程路径：$($candidate.ProcessId)"; continue }
        $role = $selected[[int]$candidate.ProcessId]
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
                gpuDedicatedBytes = $null; gpuSharedBytes = $null; gpuEnginePercentSum = $null }
        } catch { $errors += "进程退出或读取失败：$($candidate.ProcessId)：$($_.Exception.Message)" }
        finally { if ($process) { $process.Dispose() } }
    }
    # 可选 GPU 查询使用 Windows 性能计数器，按 PID 归并各适配器与引擎；查询失败保留进程数据并记录错误。
    if ($IncludeGpu -and $rows.Count) {
        try {
            $gpuSamples = @(Get-Counter -Counter @('\GPU Process Memory(*)\Dedicated Usage',
                '\GPU Process Memory(*)\Shared Usage', '\GPU Engine(*)\Utilization Percentage') -MaxSamples 1 -ErrorAction Stop | Select-Object -ExpandProperty CounterSamples)
            foreach ($gpuSample in $gpuSamples) {
                if ($gpuSample.Status -ne 0) { continue }
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
    Write-Record $sampleFile @{ runId = $runId; timeUtc = $now.ToString('o'); processes = $rows; errors = $errors
        samplingWallMs = $iterationWatch.Elapsed.TotalMilliseconds }
    $remaining = $DurationSeconds - $watch.Elapsed.TotalSeconds
    $untilNextSample = $IntervalSeconds - $iterationWatch.Elapsed.TotalSeconds
    if ($remaining -gt 0 -and $untilNextSample -gt 0) { Start-Sleep -Milliseconds ([int](1000 * [Math]::Min($untilNextSample, $remaining))) }
}
Write-Output '采样完成，可运行 -Mode Report 汇总。'
