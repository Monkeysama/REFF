param([Parameter(Mandatory=$true)][string[]]$Archives)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
# 检查归档本身，不以 staging 清单代替分发证据；正式包严格排除示例和旧引擎。
foreach ($archive in $Archives) {
    $zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($archive))
    try {
        $isTools=[IO.Path]::GetFileName($archive) -match '-tools\.zip$'
        $prefix=if($isTools){'tools/'}else{'reframework/'}
        $files=@{};$total=0L
        foreach ($entry in $zip.Entries) {
            $name=$entry.FullName.Replace('\','/')
            $checkedName=$name.TrimEnd('/')
            if (-not $name.StartsWith($prefix) -or $checkedName -match '(^|/)(\.|\.\.|)(/|$)|[\\:%<>"|?*#]|[. ](/|$)' -or $name -match '(^|/)(node_modules|cache|data)/|p1-test\.js') { throw "非法归档路径：$name" }
            if (-not $entry.Name) { continue }
            if ($files.ContainsKey($name)) { throw '归档重复文件' }
            $files[$name]=$entry;$total+=$entry.Length
        }
        if ([IO.Path]::GetFileName($archive) -match '-(examples|experimental)\.zip$') { throw '不再发布示例包或实验包' }
        if ($isTools) {
            # 工具包只包含五个明确入口，不夹带原生模块、游戏数据或网页。
            $required=@('install-reff.ps1','package-files.ps1','collect-installed-compatibility.ps1','game-support.json','README.md')
            if ($files.Count -ne $required.Count) { throw '工具包文件数量不符' }
            foreach ($name in $required) { if (-not $files.ContainsKey('tools/'+$name)) { throw "工具包缺少：$name" } }
            [pscustomobject]@{archive=[IO.Path]::GetFullPath($archive);files=$files.Count;uncompressedBytes=$total;passed=$true}
            continue
        }
        & {
            foreach ($name in $files.Keys) {
                if ($name -match '^reframework/(autorun/REFF\.examples\.lua|reff/(plugins/|examples-dev/|ui/|tools/|compatibility/|dependencies\.lock\.json$))' -or
                    ($name -match '^reframework/reff/runtime/' -and $name -ne 'reframework/reff/runtime/reff-webview2-host.exe')) { throw "Runtime 包混入旧后端或示例：$name" }
            }
            foreach ($required in @('plugins/REFF.dll','reff/runtime/reff-webview2-host.exe','autorun/REFF.lua','autorun/reff/bridge.lua','autorun/reff/sdk.lua','reff/package.json','reff/package-manifest.json','reff/licenses/THIRD-PARTY-NOTICES.txt')) {
                if (-not $files.ContainsKey('reframework/'+$required)) { throw "缺少关键文件：$required" }
            }
            if ($total -gt 30MB -or (1-$total/(402MB)) -lt .9) { throw 'Runtime 分发体积超标' }
            function Read-ZipText([string]$Name) {
                $reader=New-Object IO.StreamReader($files['reframework/'+$Name].Open())
                try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
            }
            $package=Read-ZipText 'reff/package.json' | ConvertFrom-Json
            $expectedVersion=(Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
            if ($package.version -ne $expectedVersion -or $package.embeddedShellVersion -ne $expectedVersion -or $package.resources -ne 'embedded') { throw '分发版本或内嵌资源标识与版本源不符' }
            if ($package.backend -ne 'webview2' -or $package.runtimeIncluded) { throw '后端或共享 Runtime 元数据不正确' }
            if ($package.supportScope -ne 'all-reframework-games' -or $package.version -notmatch '^\d+\.\d+\.\d+$') { throw '正式版本或支持范围元数据不正确' }
            $manifest=Read-ZipText 'reff/package-manifest.json' | ConvertFrom-Json
            $listed=@{}
            foreach ($item in $manifest) {
                $name='reframework/'+$item.path
                if ($listed.ContainsKey($name) -or -not $files.ContainsKey($name)) { throw '归档清单重复或文件缺失' }
                $listed[$name]=$true
                $stream=$files[$name].Open();$hasher=[Security.Cryptography.SHA256]::Create()
                try { $hash=([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
                finally { $hasher.Dispose();$stream.Dispose() }
                if ($hash -ne $item.sha256 -or $files[$name].Length -ne $item.bytes) { throw "归档内哈希或大小不符：$name" }
            }
            if ($files.Count -ne $listed.Count+1) { throw '归档存在未列入清单的文件' }
        }
        [pscustomobject]@{archive=[IO.Path]::GetFullPath($archive);files=$files.Count;uncompressedBytes=$total;passed=$true}
    } finally { $zip.Dispose() }
}
