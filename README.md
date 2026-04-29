## PortProbeQt（Win7 低版本友好）

### 功能（MVP）
- 网卡刷新/选择
- UDP 广播探测（需要管理员权限：Raw Socket 抓包）
- 设备表格（Name/Model/IP/SN/MAC）
- 右键复制单元格/整行
- 探测配置（保存到 exe 同目录 `config.ini`）

### 目标
- 不依赖 .NET Framework
- 使用 C++ + Qt Widgets，并在发布目录中携带 Qt/VC 运行库（类似 ConfigTool 目录结构）
- 输出 x86 + x64 两套

### 推荐环境
- Qt 5.12.x（Win7 兼容更稳；Qt 6 不建议）
- Windows 上建议使用 MSVC 工具链（与 Qt 安装包匹配）

> 当前工程已固定为 **Qt5 必须**，避免误用 Qt6 产物发布到 Win7。

### 依赖约定
工程使用 CMake 查找 Qt：
- 通过环境变量 `Qt5_DIR` 指向 Qt 的 `lib/cmake/Qt5` 目录
- 或在命令行传入 `-DQt5_DIR=...`

### 构建（示例）
以下示例分别编译 x64 / Win32（x86），生成两套输出目录。

#### x64
```powershell
mkdir build-qt-x64
cmake -S . -B build-qt-x64 -G "Visual Studio 17 2022" -A x64 -DQt5_DIR="C:/Qt/5.12.12/msvc2017_64/lib/cmake/Qt5"
cmake --build build-qt-x64 --config Release
```

#### x86（Win32）
```powershell
mkdir build-qt-x86
cmake -S . -B build-qt-x86 -G "Visual Studio 17 2022" -A Win32 -DQt5_DIR="C:/Qt/5.12.12/msvc2017/lib/cmake/Qt5"
cmake --build build-qt-x86 --config Release
```

### 打包到 dist
需要 Qt 自带的 `windeployqt`。

示例（x64）：
```powershell
pwsh packaging/pack.ps1 -Arch x64 -BuildDir build-qt-x64 -QtBinDir "C:/Qt/5.12.12/msvc2017_64/bin"
```

示例（x86）：
```powershell
pwsh packaging/pack.ps1 -Arch x86 -BuildDir build-qt-x86 -QtBinDir "C:/Qt/5.12.12/msvc2017/bin"
```

生成目录：
- `dist/x64/PortProbeQt.exe` + Qt 依赖
- `dist/x86/PortProbeQt.exe` + Qt 依赖

### Win7 无 SP1 最终发布链路（已验证）
建议使用 **Qt 5.15.2 MinGW + Inno Setup 5.6.1**：
- Qt：`D:/Qt/5.15.2/mingw81_64`
- MinGW：`D:/Qt/Tools/mingw810_64`
- CMake：`D:/Qt/Tools/CMake_64/bin/cmake.exe`
- Ninja：`D:/Qt/Tools/Ninja/ninja.exe`
- Inno：`D:/Inno Setup 5/ISCC.exe`

> 注意：MinGW 在中文路径下可能触发 `moc` 失败。若遇到此问题，请在英文目录构建（例如 `D:/portprobe_ascii`），再回项目目录执行打包脚本。

#### 1) MinGW 编译（x64）
```powershell
"D:/Qt/Tools/CMake_64/bin/cmake.exe" -S "D:/portprobe_ascii" -B "D:/portprobe_ascii/out/build/win7-mingw64" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/Qt/5.15.2/mingw81_64" -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/Ninja/ninja.exe" -DCMAKE_C_COMPILER="D:/Qt/Tools/mingw810_64/bin/gcc.exe" -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw810_64/bin/g++.exe"
"D:/Qt/Tools/CMake_64/bin/cmake.exe" --build "D:/portprobe_ascii/out/build/win7-mingw64" --config Release
```

#### 2) 生成 dist（MinGW）
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "packaging/pack.ps1" -Arch x64 -BuildDir "D:/portprobe_ascii/out/build/win7-mingw64" -QtBinDir "D:/Qt/5.15.2/mingw81_64/bin" -Toolchain mingw
```

#### 3) 生成单文件安装包（Inno Setup 5）
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "packaging/build-installer.ps1" -Arch x64 -Version 1.0.0 -IsccPath "D:/Inno Setup 5/ISCC.exe"
```

输出：
- 安装包：`installer_output/PortProbeQt_Setup_x64_v1.0.0.exe`

