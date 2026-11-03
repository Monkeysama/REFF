# REFF 可选工具

普通安装只需将核心包内的 `reframework` 文件夹合并到游戏根目录。需要备份升级、恢复或诊断时，再下载工具包。

将核心包和工具包解压到同一临时目录，得到并排的 `reframework/` 与 `tools/`，退出游戏后执行：

```powershell
& "<临时目录>\tools\install-reff.ps1" -ReframeworkRoot "<游戏目录>\reframework"
```

若核心包在其他目录，增加 `-SourceRoot "<核心包解压目录>\reframework"`。

恢复备份无需核心包：

```powershell
& "<工具目录>\install-reff.ps1" -ReframeworkRoot "<游戏目录>\reframework" -RestoreBackup "<游戏目录>\reframework\data\REFF\backups\<备份名>"
```

最小诊断：

```powershell
& "<工具目录>\collect-installed-compatibility.ps1" -ReframeworkRoot "<游戏目录>\reframework"
```

只收集版本、哈希、初始化状态与数值，不复制原始日志、设置或输入内容。

完整说明：[安装与开发](https://github.com/Monkeysama/REFF#readme)、[兼容性](https://github.com/Monkeysama/REFF/blob/main/docs/compatibility.zh-CN.md)。
