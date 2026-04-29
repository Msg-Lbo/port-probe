## Win7（低版本）验证清单

### 1) 权限
- 右键 `PortProbeQt.exe` → **以管理员身份运行**
- 若非管理员，会提示无法 Raw Socket 抓包

### 2) 基本功能
- 刷新接口：能列出本机 IPv4 网卡
- 选择网卡：下拉能切换
- 探测：能发广播并在超时内收集回复
- 列表：显示 Name/Model/IP/SN/MAC
- 复制：右键菜单复制单元格/整行到剪贴板
- 配置：修改端口/广播数据后保存到同目录 `config.ini`，重启仍生效

### 3) 依赖文件（发布目录应包含）
- `PortProbeQt.exe`
- Qt 依赖（由 `windeployqt` 生成）：
  - `Qt5Core.dll/Qt5Gui.dll/Qt5Widgets.dll/Qt5Network.dll` 等
  - `platforms/qwindows.dll`
  - `imageformats/*.dll`（可能需要）

### 4) VC 运行库（常见缺失）
若 Win7 上报错缺少 `msvcp***.dll` / `vcruntime***.dll` / `api-ms-win-crt-***.dll`：\n
- 优先做法：按你示例 ConfigTool 的方式，把对应 DLL 放在 exe 同目录\n
- 或在目标机安装对应的 Visual C++ Redistributable\n

### 5) 失败排查
- 若探测一直 0：确认目标设备确实会回包；确认端口/广播 payload 与原版本一致；确认管理员权限；确认安全软件未拦截。\n

