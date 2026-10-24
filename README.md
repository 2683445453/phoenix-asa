# phoenix · ASA

Windows x64 的 ARK: Survival Ascended 客户端扩展，包含 `phoenix.exe` 启动器、`phoenix.dll` 模块及 Lua 工具。当前版本 **2026.10.24.1**。

本项目由 [zetsr/Begeerte-ASA](https://github.com/zetsr/Begeerte-ASA) 演化而来，保留上游 MIT 许可和版权声明。此衍生版本主要扩展界面、功能、游戏适配与构建流程，与上游独立维护。

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

分发包包含 `VERSION`、源码来源 `release.json` 和逐文件 `SHA256SUMS.txt`，不含个人配置、日志和调试符号。包内程序与本次链接器输出保持一致。

```powershell
Get-FileHash .\dist\phoenix.zip -Algorithm SHA256
.\tests\run-overlay-runtime-tests.ps1
.\tests\run-backpack-tools-tests.ps1
.\tests\run-feature-expansion-tests.ps1
.\tests\run-aim-selection-tests.ps1
python .\tests\language_package_integrity.py
```

## 目录与许可

`src/` 为 DLL 与依赖，`ASA_Loader/` 为启动器，`language/`、`lua/`、`work_shop/` 为运行资源，`tests/` 为回归检查。`server/`、`_Data/` 为独立实验及源码参考，不进入默认分发包。构建与分发输出由 Git 忽略。

项目采用 [MIT License](LICENSE)。原始版权归 zetsr，后续修改归相应贡献者；源码和二进制分发均保留许可证。第三方组件及字体遵循各自许可，见源码目录和包内 `licenses/`。

界面使用 HarmonyOS Sans 字体（Copyright 2021 Huawei Device Co., Ltd.），其独立许可见 `src/external/Minimal-D3D12-Hook-ImGui/Font/LICENSE.txt` 及包内 `licenses/HarmonyOS-Sans.txt`。

公开再分发前仍需确认生成 SDK 及其中 UTF-N 代码的许可；上游 MIT 声明不替代第三方权利授权。
