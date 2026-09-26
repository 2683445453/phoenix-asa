# phoenix 注入 GPU 崩溃修正验证

发布构建：2026-09-16 22:32，Release x64。

## 交付标识

- 发布包：`dist/phoenix.zip`
- DLL 大小：11,600,896 字节
- DLL SHA256：`891aa58c3639d0650763223fe9fae865ddb6b4674f3d506a5e843695a417b5c8`
- ZIP SHA256：`12a61a55cf67e7130d69cd2345786f4ec85b38d77b5eee003e8afa2c648800e3`

完整退出游戏后替换发布包，再重新启动和注入。该包替代此前 21:51 的版本。未修改游戏的帧生成或驱动设置。

## 本次确认的事实

21:57 的进程为 PID 38888，与此前 PID 12836 不同；确认加载的是 21:51 版 DLL，初始化完成约一秒后崩溃。

进一步读取 CrashContext 和 minidump 后，两次记录均为 `CrashType=GPUCrash`、`ErrorMessage=GPU Crash dump Triggered`，崩溃线程是 `RHIInterruptThread`，不是 CPU 访问违规。Ark 原生反汇编与栈匹配：检查 fence 返回失效标记、查询设备移除状态，随后主动抛出 `0x8000`。RHIThread 的真实线程栈含 `0x887A0005`（设备已移除）。不能将这次全新进程中的崩溃归因于此前卸载残留。

崩溃时的渲染栈包含 NVIDIA Streamline／DLSS 帧生成路径。这证明存在多队列／代理交换链场景，不能据此单独认定显卡驱动或 DLSS 是根因。最初的游戏菜单尚未打开，毛玻璃画面复制也未运行；启动卡片使用的是普通 ImGui 提交。

原始诊断结果在 `release/injection-crash-2157/dump-summary.txt` 和 `ark_stack_disassembly.txt`。这些是本地排查材料，没有放进发布 ZIP。

## 修正内容

1. 移除“第一条 DIRECT 队列就是游戏队列”的选择方式。固定当前交换链的真实后缓冲，观察原生命令列表对该缓冲的最终 PRESENT 过渡及实际提交，验证设备和队列类型。每次 Present 消费一次新的证明，旧证明不可重放；无证明时跳过绘制。
2. 忽略其他活动交换链，替换时要求相同窗口或旧窗口已失效。字体和界面资源使用真实后缓冲所属设备；字体上传使用经过验证的同一队列。
3. 等待 GPU 未完成时直接跳过该帧，不重置正在使用的 command allocator，也不提前销毁字体／描述符。绘制帧槽与对应后缓冲的 fence 保持一致。
4. 字体使用 128 个独立 SRV 槽，毛玻璃复制使用后续槽位；动态字体模式在后端准备后生成 atlas，避免旧单槽接口的冲突。
5. 处理上传 Signal／等待失败、缓冲扩容失败及 Map 失败。保留仍在使用的资源，恢复确认后才复用或清理。绘制失败关闭并丢弃本帧命令列表，回滚尚未提交的毛玻璃纹理状态。
6. 首次成功提交后才安装菜单输入拦截，避免等待队列时出现看不到界面却被拦截输入的情况。日志增加交换链、设备、队列、缓冲与首次提交阶段，便于后续定位。

队列观察仅接受可证明的传统 ResourceBarrier 路径。无法观察代理对象的真实调用、仅使用增强 Barrier 或缺少完整资源状态证明时，会跳过绘制，不猜测队列。此类情况需要结合新的 DLL 日志继续确认兼容性。

## 验证

| 检查 | 结果 |
| --- | --- |
| DLL／加载器 Release 构建和加载器 `--check` | 通过；`release/injection-crash-2157/build-final.log` |
| 队列观察组件真实 WARP 提交 | 通过：错误设备／队列、无关资源、Reset、旧证明重放、多缓冲、清理及并发；运行脚本 `tests/run-swap-chain-queue-tracker-tests.ps1` |
| 实际 ImGui DX12 后端故障回归 | 通过：字体上传／扩容、Signal／等待失败、VB／IB 分配和 Map 失败、重试及清理；`release/gpu-crash/backend-tests.log` |
| 实际生产绘制流程 WARP 回归 | 通过：固定交换链、逐帧新证明、未就绪不拦截输入、GPU pending 保护、字体缩放、绘制丢弃及纹理回滚、实际 Resize；`release/injection-crash-2157/tests/overlay/overlay-runtime.log` |
| 毛玻璃复制、六轮复用及像素检查 | 通过；`release/injection-crash-2157/vibrancy-tests.log` |
| 669 个中英文键、466 条注册文本、说明、Lua、ZIP CRC 和 DLL 哈希 | 通过；`release/injection-crash-2157/package-integrity.log` |
| Git 差异空白检查 | 通过；`release/injection-crash-2157/diff-check.log` |

测试运行在独立 WARP 图形程序中，没有启动或注入游戏。本机 D3D12 debug layer 和 InfoQueue 不可用，结果来自实际图形提交、fence、Resize 和行为断言，不包含调试层验证。ASA 与当前帧生成代理的实机稳定性仍待复测，不能把上述测试当作游戏内崩溃已经消失的证明。
