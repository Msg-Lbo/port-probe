## Win7 RTM（无 SP1）验证清单

### 0) 发布链路
- 使用 Qt 5.15.2 MinGW 产物，不使用 MSVC 发布包
- 使用 Inno Setup 5.6.1 生成安装包，不使用 Inno Setup 6
- 发布目录不应包含 `vcruntime*.dll`、`msvcp*.dll`、`api-ms-win-crt*.dll`
- 发布目录不应包含 `platforms/qdirect2d.dll`

### 1) 权限
- 右键 `探测工具.exe` → **以管理员身份运行**
- 若非管理员，会提示无法 Raw Socket 抓包

### 2) 基本功能
- 刷新接口：能列出本机 IPv4 网卡
- 选择网卡：下拉能切换
- 探测：能发广播并在超时内收集回复
- 列表：显示 Name/Model/IP/SN/MAC
- 复制：右键菜单复制单元格/整行到剪贴板
- 配置：修改端口/广播数据后保存到同目录 `config.ini`，重启仍生效

### 3) 依赖文件（发布目录应包含）
- `探测工具.exe`
- Qt 依赖（由 `windeployqt` 生成）：
  - `Qt5Core.dll/Qt5Gui.dll/Qt5Widgets.dll/Qt5Network.dll` 等
  - `platforms/qwindows.dll`
  - `imageformats/*.dll`（可能需要）

### 4) 运行库
无 SP1 版本不要依赖 VC/UCRT。发布目录应包含 MinGW 运行库：
- `libgcc_s_seh-1.dll`（x64）
- `libstdc++-6.dll`
- `libwinpthread-1.dll`

### 5) 失败排查
- 若启动提示需要 Windows Service Pack 1：确认安装包由 Inno Setup 5.6.1 编译，且不是 MSVC/UCRT 发布包。
- 若探测一直 0：确认目标设备确实会回包；确认端口/广播 payload 与原版本一致；确认管理员权限；确认安全软件未拦截。
