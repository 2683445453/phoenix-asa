# 恐爪龙墙面准星与原生空格路径

用户本轮补充：关闭实验后，原生操作需要朝向出现准星的墙壁，再按空格才能进行目标跳跃。这里指原生输入，并非此前 F6 自动跳跃实验也需要墙面。

## 已确认的代码关系

### 墙面准星与待执行扑跃共用目标判断

`BPGetCrosshairAlpha` 调用 `HasValidLocationQueuedUberpounce`，按结果在 1 和 0 之间选择透明度。颜色函数也查询同一条件。因此在这段蓝图中，准星可见与目标有效性直接关联，不是一个独立的飞行能力开关。

`HasValidLocationQueuedUberpounce` 调用 `on Jump Input Released(true, false, true, ...)` 取得查询结果，再与 `CanTryUberpounce`、返回目标不是有效角色的条件相与。此处为游戏自身的 HUD 查询代码；诊断工具没有主动调用它。`CanTryUberpounce` 检查动作/运动状态，在许可分支返回 `HasQueuedUberpounce`，并明确排除 Falling 和 Swimming。抓墙时的运动模式可为 None，不能把它与自由下落等同。

### 空格按下与松开的职责不同

`BP_OnJumpPressed` 记录按键时间、设置 JumpPressedAndHeld 定时调用并请求 held=true。`JumpPressedAndHeldFn` 在时间条件允许时调用 `CheckForFallingLatchFn(true)`，所以按住期间仍可能触发自动贴墙跳跃。

`BP_OnJumpReleased` 在携带/乘客等条件通过后调用 `on Jump Input Released(false, false, false, ...)`。该函数根据运动/抓附状态、待执行目标、按住时间、朝向和功能开关，选择以下不同路径：

- **跳向已选目标**：读取 `UberpounceQueuedData`，通过 `ClientStartUberpounce` 发送 `ServerRequestUberpounce(Data,false)`。
- **从已附着位置脱离**：可走 `JumpFromUberpounce`，构造朝向/表面法线速度后发送 `ServerEndUberpounce`；也可在特定状态/方向分支进入普通 `LatchedJump(...,false,...)`。
- **自动贴墙跳跃**：原生按住检查或显式 `LatchedJump(...,true,...)` 可进入 `ServerRequestLatchedAutoJump`。

所以不能把“朝有准星的墙壁按空格成功”全部归到 `ServerRequestLatchedJump`，更不能据此否定已经实测接受的 `ServerRequestLatchedAutoJump`。本轮已展开空格分派和上述关键函数；HUD 的其他布局代码、运动更新的其他分支仍有未支持指令，输出保留 STOP。

## 无目标抓附为何会结束：找到明确的服务器检查链

`CanContinueUberpounce` 默认 retVal=true。有有效附着角色时沿对应分支返回；没有角色时取得预期抓点与当前位置，二者足够接近（代码常量 20 个引擎距离单位）时，用胶囊半径沿表面法线方向进行 `VTraceSphereBP`，长度受 `ValidateUberpounceDistance` 控制，并以命中结果更新返回值。

`UpdateUberpounceFn` 在 0x11e 调用这一检查；0x674 将其返回值纳入继续条件，0x6c0 失败时跳到 0x73d 的终止处理。在相应网络模式分支，经 0x828 计算脱离速度并于 0xab5 调用 `MultiEndUberpounce`。这条显式路径已解析，没有跨越 STOP 去猜执行顺序。

这解释了为什么只改客户端 CanContinueUberpounce 不足以保持服务器抓附。上一轮服务器反复状态 2→0、客户端每约 250 ms 重试，与该检查链相符，但仍缺少服务器函数级动态执行证据，不能将每次状态终止都唯一归因于这条分支。

`StartUberPounce` 本身会清零速度、设置运动模式 None，并开始逐帧更新。重复调用它可以产生短时停顿；只要后续检查结束了抓附，这种停顿就不是持续有效的抓附或飞行模式。`GetUberpounceTransform` 对地面/墙面主要依据旋转及法线判断，不能把目标 Type=0 简单解释为“地面”。

## 修正上一轮的诊断缺口

只读核对当前客户端对象注册表、索引/序号和虚表，确认：

| 当前对象 | ProcessEvent 虚表槽 0x50 的入口 RVA |
|---|---|
| 当前恐爪龙 | 0x3ec2f90 |
| PrimalDinoCharacter 默认对象 | 0x3ec2f90 |
| ShooterWeapon_Projectile 默认对象 | 0x6425890 |

旧观察 Hook 只挂在武器入口，初始化成功不能代表它覆盖恐爪龙。这与上一轮只有 own_* 标记、没有相应函数观察记录相符。

新版增加恐龙默认对象实际入口的只读 Hook；只有当前记录对象和已验证函数匹配才复制少量参数。原函数照常执行，不改判定、参数、返回或服务器逻辑。若今后版本两类入口相同则复用已有 Hook，避免重复挂接。卸载仍通过既有回调屏障及 MinHook 清理流程。

日志改为 schema=3，单独记录 `dino_event_hook`，并校验当前恐爪龙入口确实等于已安装入口；逐个记录函数契约是否注册。覆盖原生按下/松开、目标扑跃、普通/自动抓附跳跃请求、脱离及动画通知；速度请求额外复制实际向量。增加 HasQueuedUberpounce、排队/当前目标 Type、服务器抓附状态客户端副本、缓存表面结果和动作时间字段。

只读的“恐爪龙滑翔诊断记录”也启用此观察，仍不发请求或写运动。蓝图内部调用可能绕过 ProcessEvent，所以缺少某个内部事件不能直接断言它没有执行；新增契约和入口匹配记录可区分部分覆盖问题。

## 下一轮操作：只做原生对照

1. 完全退出客户端，用 `dist/phoenix/phoenix.exe` 加载新版，回原本地测试服骑恐爪龙。
2. 关闭无目标抓附、自动跳跃请求、抓附脱离追踪、原生运动、恐爪龙大跳和移速实验；**只开启“恐爪龙滑翔诊断记录”**。
3. 关闭菜单，先正常起跳一次让记录开始，落地后继续操作，不要打开菜单。
4. 朝一面可抓附的墙，让原生准星出现，按你刚才成功的方式操作一次空格跳跃。稍停，再把视角移到没有有效目标的方向，尝试同样操作一次。全程不用 F6；不要为了对照切换实验功能。
5. 结束后打开菜单保存日志，并说明哪一次出现准星、哪一次成功。整组须在 120 秒内完成。

这是新问题的原生输入对照，无需重做原先 A/B/C 基线。现有 F6 自动跳跃参数与行为保持原样；本轮更新只修正诊断覆盖。已有结果表明自动入口可被服务器接受，但还没有解决快速衰减或实现永久悬停。

## 文件

- 静态读取器：`release/diagnostics/decode_deino_wall_routes.py`；在原有限解析器基础上增加经对象类型校验的 MetaCast、StructConst，遇到其他未知指令停止。
- 字节码展开：`release/diagnostics/deino-wall-routes-20260926/wall-routes.json`、`wall-routes.txt`、`decode-status.json`。
- 客户端入口验证：`check_deino_event_entry.py` 和 `process-event-entry.json`；旧地址读取失败后重新捕获当前实例，没有沿用失效指针。
- 修改：`hooks.cpp/.h`、`DeinoActionTrace.inl`、`DeinoGlideProbe.inl`。
