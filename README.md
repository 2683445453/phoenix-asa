# phoenix · ASA

Windows x64 的 ARK: Survival Ascended 客户端扩展，包含 `phoenix.exe` 启动器、`phoenix.dll` 模块及 Lua 工具。当前版本 **2026.10.24**。

**分发版使用原生 EXE/DLL，不加壳；默认不要求激活、联网授权或期限验证。** 可选 Local 模式仅用于本机授权验证，未接入正式生产授权服务。

## 使用

1. 从 [Releases](https://github.com/2683445453/ASA_TEST/releases/latest) 下载并完整解压 `phoenix.zip`。
2. 保持 EXE、DLL、`language/`、`lua/`、`work_shop/` 的相对位置，运行 `phoenix.exe` 等待游戏启动。
3. **F2** 开关菜单，**Ctrl + F12** 停止内置自动化。

更新前完整退出游戏，再覆盖程序与资源；保留个人 `cfg/`、`logs/`。当前没有自动更新服务。`phoenix.exe --check` 只检查 DLL 是否存在，不加载 DLL，也不验证游戏兼容性。

SDK 与偏移依赖游戏版本，实验功能及服务器效果需游戏内确认；本次发布通过的编译和离线检查不代表全部功能已实机验收。

## 构建

需要 Windows x64、Visual Studio 2026 Build Tools（MSVC **v145**、C++ 桌面开发组件）与 Windows 10/11 SDK。PowerShell 执行：

```powershell
.\build-release.ps1 -Rebuild
# 自定义 MSBuild 路径：
.\build-release.ps1 -MSBuildPath 'D:\VS\MSBuild\Current\Bin\MSBuild.exe'
```

脚本默认构建 `Release|x64`，输出 `dist/phoenix/`、`dist/phoenix.zip` 和 `dist/phoenix.zip.sha256`。依赖已随仓库提供；必需的预编译 LuaJIT x64 对象 `src/external/LuaJIT-2.1/src/lj_vm.obj` 应保留。

分发包包含 `VERSION`、源码来源 `release.json` 和逐文件 `SHA256SUMS.txt`，不含个人配置、日志、授权密钥、数据库和调试符号。包内程序与本次链接器输出保持一致，无加壳步骤。

```powershell
Get-FileHash .\dist\phoenix.zip -Algorithm SHA256
.\tests\run-overlay-runtime-tests.ps1
.\tests\run-backpack-tools-tests.ps1
.\tests\run-feature-expansion-tests.ps1
.\tests\run-aim-selection-tests.ps1
python .\tests\language_package_integrity.py
```

可选本机授权构建：安装 Python 3.12+ 与 `licensing/server/requirements.txt` 后运行 `build-local-license.ps1 -Python 'C:\Python312\python.exe'`，生成独立的 `phoenix-local-license` 包。`.local-license/` 中的私钥和数据库仅留本机。

## 目录与许可

`src/` 为 DLL 与依赖，`ASA_Loader/` 为启动器，`language/`、`lua/`、`work_shop/` 为运行资源，`tests/` 为回归检查，`licensing/` 为可选授权模块。`server/`、`_Data/` 为独立实验及源码参考，不进入默认分发包。构建与分发输出由 Git 忽略。

原项目采用 [MIT License](LICENSE)，保留原作者声明；第三方许可见各组件及包内 `licenses/`。历史文档、诊断记录和旧包仅保留在本地归档，不随此次源码和分发版上传。
