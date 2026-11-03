# 从完整更新历史中提取当前版本；缺失或重复章节时终止，避免发布其他版本内容。
function Get-REFFReleaseNotes([string]$ChangelogPath,[string]$Version) {
    $text=[IO.File]::ReadAllText($ChangelogPath)
    $pattern='(?ms)^## v'+[regex]::Escape($Version)+'(?=[ \t\r\n]).*?(?=^## |\z)'
    $sections=[regex]::Matches($text,$pattern)
    if ($sections.Count -ne 1) { throw "更新日志必须包含唯一的 v$Version 章节" }
    return $sections[0].Value.Trim()
}
