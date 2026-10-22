# 游戏目录辅助函数只读取仓库内的 config/game-support.json；调用脚本负责决定允许哪些支持状态。
function Get-REFFGameCatalog([string]$RepositoryRoot) {
    $catalogPath = Join-Path $RepositoryRoot 'config\game-support.json'
    if (-not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) { throw "缺少游戏目录：$catalogPath" }
    $catalog = [IO.File]::ReadAllText($catalogPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($catalog.schemaVersion -ne 1 -or -not $catalog.games) { throw "游戏目录格式不受支持：$catalogPath" }
    return $catalog
}

# 从游戏根目录识别目录中的可执行文件；返回值包含进程名，供部署前只读检查使用。
function Find-REFFGame([string]$RepositoryRoot, [string]$GameRoot, [string[]]$AllowedStatuses = @('tested', 'untested')) {
    $catalog = Get-REFFGameCatalog $RepositoryRoot
    foreach ($game in $catalog.games) {
        if ([string]$game.status -notin $AllowedStatuses) { continue }
        foreach ($executable in @($game.executables)) {
            $path = Join-Path $GameRoot ([string]$executable)
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                return [pscustomobject]@{
                    target = [string]$game.target
                    name = [string]$game.name
                    status = [string]$game.status
                    executable = [string]$executable
                    executablePath = [IO.Path]::GetFullPath($path)
                    processName = [IO.Path]::GetFileNameWithoutExtension([string]$executable)
                }
            }
        }
    }
    return $null
}

# 测试状态只表达验证事实；正式安装对全部目录条目开放。
function Test-REFFGameDeploymentAllowed([string]$Status) {
    return $Status -in @('tested', 'untested')
}
