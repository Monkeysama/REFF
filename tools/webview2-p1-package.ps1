# P1 测试包只拥有 REFF 固定文件和三个官方示例；部署/恢复共用同一边界，不认领第三方插件。
function Test-REFFP1Path([string]$Relative) {
    if($Relative.Contains('..') -or $Relative.IndexOfAny([char[]]'\:%') -ge 0){return $false}
    return $Relative -cmatch '^(plugins/REFF\.dll|autorun/REFF(\.examples)?\.lua|autorun/reff/(bridge|sdk)\.lua|reff/runtime/reff-webview2-host\.exe|reff/licenses/(REFF|REFramework|WebView2|WebView2-NOTICE)\.txt|reff/dependencies\.lock\.json|reff/ui/.+|reff/plugins/example\.(vue|react|html)/.+)$'
}

# 清单路径必须逐项规范化到指定根；调用者在所有校验通过后才覆盖文件。
function Resolve-REFFP1Path([string]$Root,[string]$Relative) {
    if(-not (Test-REFFP1Path $Relative)){throw "不属于 P1 包的路径：$Relative"}
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'P1 路径越界'}
    return $full
}
