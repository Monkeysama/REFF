param([string]$BuildRoot='',[switch]$IncludeExamples,[string]$StagingRoot='')
# 常规分发统一调用 WebView2 打包入口。
& (Join-Path $PSScriptRoot 'stage-webview2.ps1') @PSBoundParameters
