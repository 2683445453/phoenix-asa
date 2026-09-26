# phoenix 界面改版验证 · 2026-09-16

本次采用现有源码上的增量修改，未删除原有功能开关，也未启动或控制游戏。

- 开发版 DLL／启动器编译成功，生成 `dist/phoenix` 与 `dist/phoenix.zip`。ZIP CRC、压缩包内和目录内二进制 SHA256 与编译输出一致；包内包含完整使用说明。
- `tests/run-interface-tests.ps1` 通过：组合键匹配、前台／输入状态／长按条件、重复绑定拒绝、菜单及停止键保留、紧急停止而保留参数和透视、扩大参数的运行限制、新旧配置读取和保存。
- `tests/run-interface-render-tests.ps1` 通过：使用实际导航和战斗／视觉／采集／自动化页面代码，替换游戏服务为离线桩，覆盖 30 个导航入口的说明开关、窄窗口、参数收起和 ImGui 栈配对。原有背包／实体列表／建筑列表／脚本／配置／语言页面的内容体在此测试中使用空桩，不算作这些原有页面的功能验证。
- 离线绘制 `home`、`aim-off`、`aim-on`、`loot-help`、`shortcuts`、`compact`、`narrow`、`palette` 图片；人工查看首页、瞄准参数、调色盘和窄窗口，修正选中复选框的默认蓝底、详情内边距、关闭颜色行后的同行残留和长标签换行。图片位于 `release/ui-work`。
- `tests/run-vibrancy-gpu-tests.ps1` 通过：DirectX 12 WARP 设备执行实际模糊着色器，核对红蓝边界的模糊混合及 80% 深灰叠色，验证资源屏障、三组缓冲六帧复用、停用及释放；检查可用调试层中的错误消息。
- `tests/run-aim-selection-tests.ps1` 与 `tests/run-gameplay-extensions-tests.ps1` 通过；后者的字号上限断言随明确的新上限从 32 改为 48。
- 三个重排页面的 `g_Config` 控件引用与改版前比对，没有丢失。差异空白检查通过（按 Windows CRLF 规则）。

这些结果验证编译、离线排版、配置逻辑及独立 GPU 渲染，尚未验证 ARK 游戏内画面、实际快捷键消息链、特定显卡驱动或服务器反馈。发布包为不启用授权的开发版；既有本地授权版未重新发布。

## 16:30 主菜单注入崩溃修复

游戏 `09.16-16.30.00.crashstack` 记录 ASA 93.24 / CL 829680、地图 `MainMenu`，异常为读取地址 `0xb0`。调用链为 `g_AActor::Tick` → `AShooterPlayerController::IsViewingInventoryUI` → 空 `UFunction` 的 `FunctionFlags`。新增快捷键逻辑将主菜单的基础控制器强制转换为游戏内控制器，没有先验证类型；因此即使未绑定功能快捷键，注入后也会查询不存在的背包接口并崩溃。

- 移除 Actor Tick 中新增的快捷键 UI 查询；改在游戏主线程的 viewport 回调执行。
- 验证控制器类型、角色和 HUD 就绪状态；没有功能绑定时跳过查询；反射函数缺失时暂停功能快捷键，不解引用空指针。
- 切图或输入界面打开时丢弃待处理切换，防止进入地图后意外启动；保留菜单键和停止自动化键。
- `tests/run-shortcut-runtime-tests.ps1` 通过：运行实际 `ShortcutRuntime.h`，使用真实 SDK 对象布局及模拟的类型查询／ProcessEvent，覆盖主菜单控制器、类型未加载、空角色／HUD、接口缺失、聊天／背包／输入阻止、自由视角、正常切换、切图后的请求清理、停止优先级。
- `tests/run-interface-tests.ps1` 再次通过，开发版重新编译并打包；既有界面与毛玻璃实现保留。

此次根因由用户本机实际游戏崩溃堆栈确认。修复后的游戏内注入仍待复测，离线回归不代表实机注入已通过。
