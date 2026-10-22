param([Parameter(Mandatory=$true)][string[]]$Archives)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
# 检查归档本身，不以 staging 清单代替分发证据；正式包严格排除示例和旧引擎。
foreach ($archive in $Archives) {
    $zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($archive))
    try {
        $files=@{};$total=0L
        foreach ($entry in $zip.Entries) {
            $name=$entry.FullName.Replace('\','/')
            $checkedName=$name.TrimEnd('/')
            if ($name -notmatch '^reframework/' -or $checkedName -match '(^|/)(\.|\.\.|)(/|$)|[\\:%<>"|?*#]|[. ](/|$)' -or $name -match '(^|/)(node_modules|cache|data)/|p1-test\.js') { throw "非法归档路径：$name" }
            if (-not $entry.Name) { continue }
            if ($files.ContainsKey($name)) { throw '归档重复文件' }
            $files[$name]=$entry;$total+=$entry.Length
        }
        if ([IO.Path]::GetFileName($archive) -match '-(examples|experimental)\.zip$') { throw '不再发布示例包或实验包' }
        & {
            foreach ($name in $files.Keys) {
                if ($name -match '^reframework/(autorun/REFF\.examples\.lua|reff/(plugins/|examples-dev/))' -or
                    ($name -match '^reframework/reff/runtime/' -and $name -ne 'reframework/reff/runtime/reff-webview2-host.exe')) { throw "Runtime 包混入旧后端或示例：$name" }
            }
            foreach ($required in @('plugins/REFF.dll','reff/runtime/reff-webview2-host.exe','reff/ui/index.html','autorun/REFF.lua','reff/tools/install-reff.ps1','reff/package.json','reff/package-manifest.json','reff/licenses/WebView2.txt','reff/licenses/Web-third-party.txt')) {
                if (-not $files.ContainsKey('reframework/'+$required)) { throw "缺少关键文件：$required" }
            }
            if ($total -gt 30MB -or (1-$total/(402MB)) -lt .9) { throw 'Runtime 分发体积超标' }
            function Read-ZipText([string]$Name) {
                $reader=New-Object IO.StreamReader($files['reframework/'+$Name].Open())
                try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
            }
            $package=Read-ZipText 'reff/package.json' | ConvertFrom-Json
            $expectedVersion=(Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
            $uiVersion=Read-ZipText 'reff/ui/version.json' | ConvertFrom-Json
            if ($package.version -ne $expectedVersion -or $uiVersion.version -ne $expectedVersion) { throw '分发版本与版本源不符' }
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
