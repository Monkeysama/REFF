param([Parameter(Mandatory=$true)][string[]]$Archives)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
# 检查归档本身，不以 staging 清单代替分发证据；增量示例严格排除核心文件。
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
        $examples=$archive -match '-examples\.zip$'
        if ($examples) {
            foreach ($name in $files.Keys) {
                if ($name -notmatch '^reframework/(autorun/REFF\.examples\.lua$|reff/(plugins/example\.(vue|react|html)/|examples-dev/))') { throw "示例包混入核心文件：$name" }
            }
            foreach ($id in @('vue','react','html')) {
                foreach ($suffix in @('manifest.json','ui/dist/index.html')) {
                    if (-not $files.ContainsKey("reframework/reff/plugins/example.$id/$suffix")) { throw '示例缺少构建产物' }
                }
                foreach ($suffix in @('package.json','pnpm-lock.yaml','vite.config.ts','tsconfig.json')) {
                    if (-not $files.ContainsKey("reframework/reff/examples-dev/web/plugins/example.$id/$suffix")) { throw "示例缺少独立开发配置：$id/$suffix" }
                }
            }
        } else {
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
            if ($package.backend -ne 'webview2' -or $package.runtimeIncluded) { throw '后端或共享 Runtime 元数据不正确' }
            $experimental=$archive -match '-experimental\.zip$'
            if ([bool]$package.experimentalGames -ne $experimental) { throw '实验标志与归档名不符' }
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
        [pscustomobject]@{archive=[IO.Path]::GetFullPath($archive);files=$files.Count;uncompressedBytes=$total;examples=$examples;passed=$true}
    } finally { $zip.Dispose() }
}
