# Begeerte-ASA-main\docs\codex.md总结开发方法然后将泰克步枪也加入...

Exported: 2026/9/20 12:31:53

---

**User:**

Begeerte-ASA-main\docs\codex.md总结开发方法然后将泰克步枪也加入投射物自动预判中，然后修复一下泰克狙击枪不能连发的问题，并把除了恐爪龙跳跃倍率的其他恐爪龙功能删除，以及检查一下为什么打开传服列表后点击加入不能传服的问题，可以使用本地服务器辅助开发，如果有工具可以便捷开发流程也可以下载

---

**Assistant:**

我会先按 `Begeerte-ASA-main/docs/codex.md` 梳理项目约定并定位这四类功能（泰克武器、恐爪龙、传服列表），然后直接修改并用现有构建/本地服流程验证。

### Sub-Agent: Context Gatherer

**Explanation:** 这是一个陌生且涉及多个模块的代码库，需要先定位项目、理解 codex.md 的开发约定，并梳理武器预判、连发、恐爪龙和跨服传送相关实现。

**Prompt:**

在多根工作区 d:\Programs 下定位 Begeerte-ASA-main。阅读 docs\codex.md 并总结必须遵循的开发/验证方法；随后定位并分析：1) 投射物自动预判及支持的武器，如何加入泰克步枪；2) 泰克狙击枪不能连发的相关逻辑；3) 恐爪龙功能及跳跃倍率配置，哪些代码应保留/删除；4) 打开传服列表后点击加入却不能传服的完整调用链和最可能根因。请返回相关文件、符号、关键代码范围、建议修改方案、可用的本地服务器与构建测试命令。不要改文件。注意只访问 d:\Programs 内路径。

**Response:**

已在限定路径 `D:\Programs` 内完成只读分析，未修改文件。

## 结论先行
1. **投射物预判目前实际上已经覆盖泰克步枪**：入口不是武器白名单，而是所有 `AShooterWeapon_Projectile`。运行时证据显示 `WeapTekRifle_C` 继承 `ShooterWeapon_Projectile`，`ProjectileClass=ProjTekRifle_C`，且 `bUseBPSelectProjectileToFire=false`，应直接进入现有 `ProjectileProfile`。若游戏里未生效，应查 hook/方向事件是否命中，而不是再复制一套泰克步枪弹道算法。
2. **泰克狙击枪不能连发的主因不是 `TimeBetweenShots`**：本地射击间隔确实被缩短，但实测服务器热量仍上升、仍按原冷却拒绝。当前“无热”只拦截客户端当前武器的 `BPFireWeapon`，远端服务器会独立执行加热/可开火状态机；客户端强清同步热量只会产生无伤害空枪。远程服必须有服务端插件/authority hook，否则应明确标为不支持。
3. **恐爪龙大跳链路有效，建议保留**：`UberpounceJumpSpeed *= DeinonychusJumpMultiplier`，范围 1–5、默认 2；历史实测 3.5 倍得到 10500，服务器记录约 46m/s 上升速度。建议保留大跳、倍率、恢复补丁及测试；若目标只保留已验证功能，删除实验性本地主机 `MOVE_Flying` 和管理员 `RemoteServerCheat("fly"/"walk")` 两套“飞行”入口。
4. **传服列表“能开但点击加入不传服”的最可能根因非常明确**：直接入口只用人物背包调用 `BPShowUIScene` 创建列表，代码还明确提示“未提交上传”；它没有使用真实方尖碑 tribute inventory，也没有调用 `ServerUploadCurrentCharacterAndItems`，因此缺少服务器确认的上传角色/转服上下文。列表 UI 可浏览，但 Join 无有效 transfer payload/token，服务器不会传服。

## docs/codex.md 必须遵循的开发/验证方法
- **先读当前游戏版本的 live class/Blueprint 字段与函数字节码，再改代码**；不要只按 SDK 名称猜。泰克步枪、狙击枪、恐爪龙和列表 UI 都是通过运行时类、字段、函数大小/flags、字节码签名核对后接入。
- **只改实例，不写 CDO**；反射属性必须校验类型、偏移、数组维度、对象 serial，并在关闭功能、换枪/换坐骑、失焦、暂停、卸载时恢复原值。现有 `ExpansionPatch` 是应复用的安全模式。
- **客户端表现与服务器接受必须分开验证**。离线单测/编译通过不能证明联机伤害、热量、扣弹、跳跃或传服成功；必须同时采集客户端与服务器状态，并让用户确认实际伤害/行为。
- **不能把“面板打开”“本地字段变了”表述为功能成功**。传服要看到服务器上传确认和实际 travel；射速要看到服务器扣弹与持续伤害；跳跃要看到服务器速度/轨迹。
- 每项修改至少执行：目标规则测试 → 相关 UI/配置测试（如涉及）→ Release 构建/loader `--check`/打包校验 → 游戏内双端实测。失败时保留原生流程，不制造无伤害射击或伪造本地 inventory。
- `codex.md` 的历史结论：泰克步枪持续伤害已实测；恐爪龙大跳已获双端证据；泰克狙击枪服务器过热仍未解决；直接传服列表当时只验证“能打开”，并未验证实际传服。

## 1) 投射物自动预判与泰克步枪
调用链：
- 菜单 `Aimbot_Menu.cpp`：`bProjectilePrediction`、最大飞行时间、额外延迟。
- `Aimbot.cpp` 在普通方向请求、投射物 ProcessEvent 和触发路径中调用 `UsesProjectileFlight` / `PrepareProjectileShot`。
- `UsesProjectileFlight` 接受所有 `AShooterWeapon_Projectile`，仅排除泰克弓 accessory 的即时命中模式。
- `ProjectileProfile` 从 projectile CDO 的 movement component 读取 `InitialSpeed`、`ProjectileGravityScale`，叠加武器速度/重力 modifier；普通弓额外按蓄力插值。
- `PredictProjectile` 用目标当前速度、世界重力、延迟和时限解最早拦截；随后 `ProjectilePathClear` 分段 trace，遮挡时保留原生发射。
- 泰克弓是唯一专门 profile：因为其 `ProjectileClass` 为空，需从 `AmmoConfig` 解析金属/爆炸/麻醉/火焰箭；即时命中 accessory 不做飞行预判。

已知支持范围：所有具有有效 projectile CDO + movement component 的投射物武器；菜单文案明确列出流星锤、复合弓、弩、泰克弓实体箭模式。泰克步枪满足通用条件：`WeapTekRifle_C -> ShooterWeapon_Projectile -> ProjTekRifle_C`。

建议修改：
- **功能代码原则上无需新增泰克步枪特判**。可增加 `IsTekRifle` 仅用于状态文案/诊断计数和回归断言，避免重复算法。
- 增加运行时诊断：记录 class、ProjectileClass、speed、gravity，以及 Aimbot 中三条触发路径哪条命中。若 `ProjectileProfile` 成功但不偏转，问题在 `GetAdjustedAim`/投射物事件 hook；若 profile 失败，再检查当前版本 `ProjTekRifle_C` CDO movement。
- 增加 fixture/test，断言泰克步枪不是 BP-select 分支、应走通用 profile；保留泰克弓专用解析。

## 2) 泰克狙击枪不能连发
相关逻辑分两层：
- `GameplayExtensionsTick`：对 instant/projectile 武器反射读取 `WeaponConfig.TimeBetweenShots`，按倍率相除；切换对象或状态不就绪时恢复。此层只改变本地请求频率。
- `TekEquipmentTick`：识别 `WeapTekRifle_C` / `WeapTekSniper_C`，严格验证 `BPFireWeapon` 是否只有 `AddHeat` 后才允许 `SuppressTekHeatEvent`。仅 authority 时才把热量字段改为 0；狙击枪另改 `OverheatDuration=0`、`OverheatCompleted=1`。

历史实测：狙击枪本地间隔从约 6 秒降到约 2 秒，但服务器热量继续上升，仍按原冷却状态处理。说明狙击枪存在服务端独立 AddHeat/BPWeaponCanFire/ubergraph 路径，客户端 ProcessEvent suppression 不会影响远端服务器。

建议：
- 自有专服：在服务端安装对应插件，在 authority 上 hook 经签名验证的 `WeapTekSniper_C::AddHeat` 或 server-side `BPFireWeapon/BPWeaponCanFire`；同时观测 ammo、heat、damage，避免只看动画。
- 非自有远程服：不要尝试清客户端复制热量；保留当前防“无伤害空枪”策略。UI 应改成“泰克步枪已验证；泰克狙击枪远程服不支持/仅 authority”。
- 保留 `TimeBetweenShots` 通用恢复逻辑、严格字节码验证和 rifle 处理；不要把 sniper 的 `OverheatDuration/Completed` 当成远程解决方案。若不做服务端组件，建议从远程支持文案中删除狙击枪“无热/连发”承诺。

## 3) 恐爪龙功能与保留/删除边界
现有三条独立功能：
- 大跳：`bDeinonychusBigJump` + `DeinonychusJumpMultiplier`，命中当前坐骑类名后通过 slot 22 成倍修改 `UberpounceJumpSpeed`；原生 `ComputeJumpVector -> ServerRequestJump(Velocity)` 仍负责输入、体力和服务端请求。
- 本地主机实验飞行：只有 pawn authority 时将 movement mode 改为 `MOVE_Flying`，退出恢复原模式。
- 管理员原生飞/走：按钮排队，游戏线程调用 `RemoteServerCheat("fly"/"walk")`，依赖真实管理员权限，并会改变真实坐骑/人物状态。

建议保留：`bDeinonychusBigJump`、倍率 1–5/默认 2、UI slider、`ExpansionHasClass`、slot 22、`ExpansionPatch` 原值跟踪/恢复、StopAll/配置测试和双端跳跃验证。

若产品目标是“保留已验证的大跳、去掉混乱的恐爪龙飞行”，建议删除：`bDeinonychusFlight`、菜单飞行 checkbox、`Fly`/`ExpansionMove` 中仅恐爪龙飞行分支，以及 `PlacementExperiment.inl` 中 `AdminDeinonychusFlight`、两个按钮、`RemoteServerCheat` 和对应文本/声明。不要删除 `PlacementExperiment` 的浮空建筑部分。同步清理 `FeatureShortcuts::StopAll` 和 `Wanted` 引用；旧配置键可静默忽略。

## 4) 传服完整调用链与根因
当前实验链：
`Misc_Menu::DrawCustomButton("直接打开原生传服列表")`
→ `RequestOpenNativeTransferPanel()` 设置 atomic
→ 游戏线程 `WorldUtilitiesTick()` consume
→ `PC->GetShooterHud()`
→ `HUD->InventoryUITemplate` CDO
→ `UUI_Inventory::ListSessionsUITemplate`
→ `HUD->BPShowUIScene(Template, Human->MyInventoryComponent, nullptr, 0, 0)`
→ `UUI_ListSessions` / 实际 cluster subclass 创建、SetupLayout、搜索和选择
→ Join 按钮/slot selection 进入原生 session/cluster game-session 路径。

缺失的正常上传链应为：
真实已复制方尖碑/tribute inventory
→ `ServerActorViewRemoteInventory` + `ServerRequestActorItems`
→ 原生 `ShowInventory(Best)`
→ 用户确认上传
→ `AShooterPlayerController::ServerUploadCurrentCharacterAndItems(Best, mode)`（或对应角色上传 RPC）
→ 服务器检查距离、tribute inventory、cluster id、冷却、物品/角色资格并持久化
→ `ClientOnCurrentCharacterAndItemsUploaded(TransferringPlayerDataId)`
→ 创建带有效 `UUI_ClusterServersListSessions::ClustersServersListGameSession` 和 transfer context 的集群列表
→ 选择目标并执行原生 travel。

当前实现直接传入 `Human->MyInventoryComponent`，没有真实 tribute inventory、没有上传 RPC/服务器确认、没有 transferring player id；状态文本也明确写着“未提交上传”。因此最可能根因不是按钮事件没绑定，而是**绕过了上传前置状态机**。

建议修改：
- 最安全：删除/隐藏“直接打开原生传服列表（实验）”，统一调用已经存在的 `RequestOpenUploadTerminal()`，只从真实已加载方尖碑 `ShowInventory(Best)` 进入原生上传链。
- 若必须保留快捷入口：先寻找真实 tribute terminal，打开原生 inventory；由用户在原生 UI 确认上传。不要用人物 backpack 冒充 terminal，也不要在客户端自行伪造 transfer id。
- 只有在明确捕获 `ClientOnCurrentCharacterAndItemsUploaded` 成功后才显示/进入 cluster list；记录 upload RPC、成功回调、cluster session pointer、join/travel callback。失败应展示服务器拒绝原因。

## 本地服务器与验证命令
仓库内记录的测试服配置为 `PhoenixFeatureTest`：客户端控制台 `open 127.0.0.1:7797`，无加入密码；Query 27098、RCON 27097。启动脚本位于 `D:\Programs\Begeerte-ASA-main\release\diagnostics\start-feature-server.ps1`，但脚本源码硬编码 `E:\asa-localserver`。遵照本次“只访问 D:\Programs”限制，我没有访问/启动 E 盘服务器，也不能确认它当前在线；若要严格保持路径限制，应先把 ASA server 安装/副本放到 `D:\Programs` 并参数化 `$sourceRoot`，再运行脚本。脚本还是一次性创建逻辑，目标测试目录已存在时会主动报错，不能盲目重复执行。

只读分析未运行测试。建议修改后的命令（均从仓库根目录执行）：
```powershell
Set-Location 'D:\Programs\Begeerte-ASA-main'
.\tests\run-gameplay-extensions-tests.ps1   # 弹道解、泰克弓规则、射速/配置
.\tests\run-aim-selection-tests.ps1         # 瞄准与方向选择回归
.\tests\run-feature-expansion-tests.ps1     # 恐爪龙配置、patch 恢复、StopAll
.\tests\run-interface-tests.ps1             # 若改菜单/功能入口
.\tests\run-interface-render-tests.ps1      # 若改 UI 文案/控件
$env:PYTHONIOENCODING='utf-8'; python .\tests\language_package_integrity.py
.\build-release.ps1 -Rebuild                 # Release x64 DLL + loader + --check + zip/hash
```
游戏内验证：连接 `open 127.0.0.1:7797`；泰克步枪/狙击枪同时采集 client/server 的 TimeBetweenShots、heat、ammo 和目标伤害；恐爪龙分别测 1x/2x/3.5x 并核对 server velocity/trajectory；传服必须用同 ClusterId 的至少两台服务端，看到 upload success callback 与实际 travel 才算通过。单台 `PhoenixFeatureTest` 只能验证上传前置和 UI，不能完整证明跨服。

未能验证：本地服务当前进程状态、两台同集群服务器是否存在、当前客户端版本中 Join 后的 native 内部非导出实现；但现有源码和 RPC 声明足以确定直接入口缺失上传上下文是最高概率根因。

我先在 `D:\Programs` 内精确定位目标仓库，并并行确认 `docs\codex.md` 与项目结构；随后只读取命中功能的最小代码范围。

初步命中显示四项分别集中在 `ProjectileAim/TekBowProfile`、`GameplayExtensions/TekEquipment`、`FeatureExpansion/PlacementExperiment`、`WorldUtilities`。我现在补齐入口到 SDK 的调用细节，并从 `codex.md` 提取强制验证规则与本地服务器命令。

已确认 `codex.md` 的关键原则：先用运行时证据确认真实类/字段/字节码，只修改实例且可恢复，并以服务器扣弹、伤害、热量和实际 travel 作为成功标准。当前根因也已明确：泰克步枪其实能进入通用投射物路径，但缺少显式识别/回归保障；狙击枪受服务端过热状态机限制；“直接传服列表”绕过了真实方尖碑上传上下文。下面我会按这些边界修改，而不是用仅客户端看起来有效的假修复。

我会同步做四组修改：显式标注泰克步枪的弹道预判路径；阻止远程泰克狙击枪产生“本地连发但服务器拒绝”的空枪，并保留本地主机 authority 的真连续射击；彻底删除恐爪龙飞行/管理员飞行，只保留大跳倍率；删除无上传上下文的“直接列表”，统一走真实方尖碑终端。由于狙击枪热量由远程服务器维护，客户端无法对未安装服务端组件的服务器强制取消过热，这一限制会在界面中明确显示。

---

**User:**

为什么泰克步枪就可以拦截热量，狙击枪不行

---

**Assistant:**

核心区别是：**当前拦截发生在客户端进程里，而泰克狙击枪的过热判定由服务器独立维护。**

- **泰克步枪**：`BPFireWeapon → AddHeat()` 在当前游戏版本中主要走持有者客户端的热量路径。拦截客户端 `BPFireWeapon` 后，实测服务器热量保持为 0，同时服务器正常扣弹并造成持续伤害，因此这一方案有效。
- **泰克狙击枪**：调用是 `BPFireWeapon → AddHeat(true)`。这个 `true` 会进入更完整的服务器状态机，服务器会独立更新：
  - `OverheatingAmount`
  - `LastShotTime`
  - `LastOverheatedTime`
  - `Overheated / OverheatCompleted`
  - `BPWeaponCanFire`

所以即使客户端拦截了 `BPFireWeapon`，服务器进程中的 `AddHeat(true)` 仍然执行，之后服务器会拒绝过快的射击。强行把客户端同步下来的热量清零，只会出现“有开枪动画但不扣弹、没伤害”的空枪。

简单说：

```text
泰克步枪：
客户端 AddHeat 被拦截
→ 服务器没有独立重建热量
→ 连发仍被服务器接受

泰克狙击枪：
客户端 AddHeat 被拦截
→ 服务器仍执行自己的 AddHeat(true)
→ BPWeaponCanFire 拒绝后续射击
```

这不是多找一个客户端特征码就能解决的，因为客户端无法修改远程服务器内存。要让狙击枪真正连续射击，需要：

1. **本地主机/单机**：当前进程有 `ROLE_Authority`，可以直接拦截并清除服务器侧热量。
2. **独立服务器**：安装服务端插件，在服务端拦截狙击枪的 `AddHeat(true)` 或调整其 authority 状态机。
3. **没有服务端组件的远程服务器**：无法实现真实连续射击，只能保留服务器原生间隔，避免产生无伤害空枪。

---

**User:**

那浮空建筑能行吗，可以让地基之类的建筑直接无视建筑要求直接建造吗

---

**Assistant:**

可以，但取决于服务器权限。**当前“浮空建筑”实验只能修改客户端预览和发送出去的 `bIgnoreGroundRequirement`，不能保证绕过服务器最终校验。**

建筑放置通常至少有两层：

1. **客户端预览**
   - 地基显示红/绿
   - `IsAllowedToBuild`
   - `bIgnoreGroundRequirement`
2. **服务器权威校验**
   - 地面与支撑关系
   - 建筑碰撞、禁建区
   - 距离、部落权限、建筑数量
   - Snap 点是否合法
   - 放置后的支撑图检查

因此只设置 `bIgnoreGroundRequirement=true`，可能让地基预览变绿，但远程服务器仍可能拒绝，或者短暂生成后立即拆除。

实际可行性：

- **单机/本地主机**：可以在 authority 侧改校验，基本可以实现地基直接悬空。
- **自己管理的独立服务器**：可以通过服务端插件拦截 `ServerRequestPlaceStructure` / `IsAllowedToBuild`，仅忽略地面与支撑要求；这是最可靠的方法。
- **没有插件的远程服务器**：不能稳定实现。客户端参数可能被服务器重新计算，不能承诺成功。

而且“无视全部建筑要求”风险较高，可能导致建筑重叠、禁建区放置或服务器存档异常。更合适的是只放宽：

```text
地面要求
支撑要求
地基坡度
```

仍保留碰撞、地图禁建区、距离、权限和建筑上限检查。这样可以实现“空中放地基”，又不容易破坏服务器。当前仓库中的实验还没有做到服务器侧这一层，所以它目前不能算已经成功。

---

**User:**

那投射物子弹速度应该可以修改吧，我观察到物品特性中有一个特性叫反重力，可以增加50%投射物速度并减少50%下坠，能直接定位到这个函数进行修改吗

---

**Assistant:**

可以调查，而且优先应定位“反重力”特性实际修改的字段或 Buff，而不是先猜函数。我要先在现有 SDK、运行时转储和物品特性代码中追踪它是修改 `ProjectileSpeed`、武器倍率，还是投射物移动组件的速度/重力；这也决定远程服务器是否会接受。

### Sub-Agent: Context Gatherer

**Explanation:** 物品特性与投射物属性是当前代码库中尚未梳理的新功能区域，需要先定位真实类、字段、函数及网络 authority 边界。

**Prompt:**

在 D:\Programs\Begeerte-ASA-main 内只读调查 ASA 物品特性“反重力”（可能英文 Anti-Gravity/Antigravity/AntiGravity/Gravity 等）如何实现。搜索源码、SDK、release/diagnostics 运行时转储和 docs/codex.md。重点回答：1) 对应特性类/数据表/Buff/技能名称；2) 50% 投射物速度与 -50% 下坠最终写到哪些字段或调用哪些函数；3) 是武器实例、弹药、AShooterProjectile CDO/实例还是 movement component；4) 客户端与服务器分别在哪里计算，远程服是否可修改且保持真实伤害；5) 当前 ProjectileAim.inl 如何读取 speed/gravity，若特性已生效是否已自动纳入预判；6) 推荐的最小安全修改方案和相关文件/符号。不要改文件。

**Response:**

只读调查结论（未改文件；另用仓库现有诊断逻辑对当前 ArkAscended 进程做了纯 ReadProcessMemory，不调用游戏函数、不写内存）：

1) 对应资产/类，不是 Tek Gravity Grenade
- 完整档物品资产：`/Game/Packs/Wasteland/CoreBlueprints/ItemTraits/Items/PrimalItem_ItemTrait_Projectile_Antigrav`，类 `PrimalItem_ItemTrait_Projectile_Antigrav_C`。
- 中档/低档：同名 `_Moderate`、`_Lesser`。
- 真正 Trait UObject：`.../ItemTraits/Traits/PrimalItemTrait_Projectile_Antigrav`，类 `PrimalItemTrait_Projectile_Antigrav_C`；中/低档继承完整档，再继承 `PrimalItemTrait_Base_C > UPrimalItemTrait`。
- Trait 内部 `FName` 是 **`Antigrav`**。不要与 Extinction 的 `PrimalItem_WeaponTekGravityGrenade_C`、`ProjTekGravityGrenade[_Repel]`、`Buff_TekGravityGrenadeForce` 混淆，它们是另一套手雷玩法。
- 没找到 Antigrav 专用 DataTable、Buff 或 Skill。实现走 `UPrimalItemTrait::Modifiers`，不是 `FPrimalSkill::GrantedModifiersPerRank`。`UPrimalItemTrait` 虽有通用 `BuffsToAddWhenEquipped` 字段，但当前原生消费链只检查 ItemTrait；现有 dump 未展开 Antigrav CDO，因而不能从持久文件独立证明该数组是否绝对为空。
- `docs/codex.md`（1677 行）没有 `Antigrav/反重力/PrimalItemTrait_Projectile` 记录；`release/diagnostics` 的现有持久 JSON/log 也没有 Antigrav 快照。直接证据来自根目录 `.codex-tek-projectile-types.log` 和当前进程 NamePool/native body。

2) 50% 弹速与 -50% 下坠的真正消费点
- 当前游戏 build 25338022 的只读反汇编确认：
  - `AShooterWeapon_Projectile::GetProjectileSpeedModifier()` native body（本 build RVA `0x70A0FE0`）从 `AShooterWeapon::AssociatedPrimalItem`（偏移 `0x710`）取物品，默认返回 `1.0`，调用 `HasItemTrait(FName("Antigrav"))`，再调用 `UPrimalItem::GetItemTraitModifier(FName("Antigrav"), ..., ModifierIndex=0, bInitOutValue=false)`，返回有效速度乘数。
  - `GetProjectileGravityModifier()`（RVA `0x70A1940`）完全同链路，但 `ModifierIndex=1`，返回有效重力乘数。
- 因而完整档描述“+50% projectile speed / -50% drop”的有效结果应是 **speed × 1.5、gravity × 0.5**。现有文件 dump 没有展开 CDO `Modifiers[]`，所以不能仅据持久文件断言 CDO 原始 `Value/StackingType` 的编码形式；可以确定的是两个 getter 最终必须返回 1.5/0.5 才实现该语义，且索引顺序已由 native body 证实。
- 它不是简单把 50% 写进弹药或 projectile CDO。速度/重力首先由上述 weapon getter 返回。发射 API 有 `ServerFireProjectileEx/ClientsFireProjectileEx(..., Speed, ...)`、`BPSelectProjectileToFire[_V2](..., OverrideMaxSpeed, ...)`；最终飞行状态属于生成出的 projectile 实例的 `MovementComp`：基准字段是 `InitialSpeed`、`MaxSpeed`、`ProjectileGravityScale` 和当前 `Velocity`。现有证据没有定位到一个“永久写 `ProjectileGravityScale=0.5`”的指令，因此更准确的表述是：实际物理等价于 `final speed = base/final RPC speed × speed modifier`、`final gravity = world gravity × projectile MovementComp.ProjectileGravityScale × gravity modifier`；不要声称 Trait 直接改了 CDO。

3) 对象归属
- Trait 挂在**武器关联的 `UPrimalItem` 实例**上：`UPrimalItem::MyItemTraits`（偏移 `0x3D0`）；武器实例通过 `AShooterWeapon::AssociatedPrimalItem` 访问它。
- 不是 ammo 本身，也不是全局 `AShooterProjectile` CDO。Projectile CDO 只提供该弹种的基准 `MovementComp->InitialSpeed/MaxSpeed/ProjectileGravityScale`。
- 生成后的 `AShooterProjectile` 实例有自己的 `MovementComp` 和指回 weapon 的弱引用；真正轨迹由该 movement component/实例速度推进。

4) 客户端/服务器与真实伤害
- 客户端和服务器各自都有 weapon/item/projectile 对象并会走相同 getter；网络入口明确分为 `ServerFireProjectile[Ex]` 与 `ClientsFireProjectile[Ex]`。`bSpawnProjectileOnClient` 可产生客户端预测/视觉 projectile，但服务器 projectile、碰撞、命中和伤害仍是权威。
- 仅在远程服客户端本地改 Trait CDO、weapon getter、projectile CDO 或 movement，只会造成预测/视觉差异；不能让不受控远程服务器的权威轨迹/命中/真实伤害随之改变。客户端可以像当前 aim hook 一样改发给服务器的 `ShootDir`，服务器在 `ServerMaxProjectileAngleError/OriginError` 范围内接受后，命中仍是服务器真实伤害；这不等于客户端能改权威速度/重力。
- 如果你控制远程服务器，可在服务器侧改 getter/Antigrav modifier，使服务器生成的 projectile 按新轨迹命中，伤害保持真实；只改速度和重力 getter，不碰 `AShooterProjectile::WeaponConfig`、DamageType、直接/爆炸伤害乘数即可保持原伤害数值。客户端最好同步同一算法，否则会有显示和预判偏差。

5) 当前 `ProjectileAim.inl`
- 非 TekBow：从 `ProjectileWeapon->ProjectileClass` 的 **AShooterProjectile CDO** 取 `Default->MovementComp->InitialSpeed`；弓会先按拉弓时间/`ProjectileSpeed` 算基速。若 RPC `ExplicitSpeed > 1`，它优先使用该值；否则乘 `ProjectileWeapon->GetProjectileSpeedModifier()`。
- 重力：读取 WorldSettings 的 `GlobalGravityZ` 或 `WorldGravityZ`，乘 CDO `MovementComp->ProjectileGravityScale`，再乘 `ProjectileWeapon->GetProjectileGravityModifier()`。
- 所以 Antigrav 已合法挂在当前 weapon 的 `AssociatedPrimalItem` 时，两个 getter 会自动返回其有效乘数，**现有预判已自动纳入，不应再手工 ×1.5/×0.5，否则双算**。
- 唯一 caveat：Extended RPC 的 `ExplicitSpeed` 分支故意跳过 speed getter；如果该参数是 native 已算好的最终速度，这是正确的。若某特殊武器传的是未修正基速，则会漏算，需要用一次带 Antigrav 的 `Server/ClientsFireProjectileEx` runtime capture 对比确认；根目录旧 capture 显示 `Projectile snapshots captured: 0`，当前仓库没有这项实测证据。重力 getter无论如何都会调用。

6) 推荐最小安全方案
- **只为预判适配：不改。** 保持 `ProjectileProfile()` 调用两个 native getter；最多加只读诊断（weapon 类、AssociatedPrimalItem、`HasItemTrait("Antigrav")`、两个 getter 返回值、ExplicitSpeed），不要再叠乘。
- **要改变实际玩法且控制服务器：** 最小侵入是在服务器权威侧拦截/包装 `AShooterWeapon_Projectile::GetProjectileSpeedModifier` 和 `GetProjectileGravityModifier`，仅当 `AssociatedPrimalItem->HasItemTrait("Antigrav")` 时返回目标值；authority gate、有限值/范围校验，并保留原返回值。客户端同步同一返回规则供显示/预判。这样沿用原生 spawn、RPC、碰撞和伤害路径。
- 不推荐改全局 `AShooterProjectile` CDO 或 `UProjectileMovementComponent` CDO（污染同弹种/全武器）；也不推荐 spawn 后只改 `InitialSpeed`（组件激活后它未必改变当前 `Velocity`）。若不得不逐 projectile 改，必须只改该实例并同步 `Velocity`、`InitialSpeed`、`MaxSpeed`、`ProjectileGravityScale`，服务器权威执行、客户端镜像，且处理对象 serial/销毁/恢复；风险高于 getter 方案。
- 相关项目符号：`ProjectileProfile`、`PrepareProjectileShot`、`OverrideProjectileEvent`、`InitializeProjectileEvents`；SDK 的 `UPrimalItem::MyItemTraits/GetItemTraitModifier/HasItemTrait`、`UPrimalItem_ItemTrait::MyItemTrait`、`UPrimalItemTrait::Modifiers`、`AShooterWeapon::AssociatedPrimalItem`、`AShooterWeapon_Projectile::{GetProjectileSpeedModifier,GetProjectileGravityModifier,ServerFireProjectileEx,ClientsFireProjectileEx}`、`AShooterProjectile::{MovementComp,Weapon}`、`UProjectileMovementComponent::{InitialSpeed,MaxSpeed,ProjectileGravityScale,SetVelocityInLocalSpace}`。

我先定位目标目录的源码、SDK、运行时转储与 `docs/codex.md`，再用一轮宽泛关键词检索锁定反重力特性及弹道读取链路；全程只读。

已确认 `ProjectileAim.inl` 和 Tek Gravity Grenade SDK 类文件；首轮文本检索因工作区绝对 glob 未命中，我正改用工作区级符号检索，并直接核对瞄准实现与 codex 中的现有研究记录。

关键对象已锁定：物品是 `PrimalItem_ItemTrait_Projectile_Antigrav[_Moderate/_Lesser]_C`，实际特性对象是 `PrimalItemTrait_Projectile_Antigrav[_Moderate/_Lesser]_C`。下一步只需读取该对象的运行时详情、SDK 特性结构和现有 DLC 特性扫描代码，确认两个 50% 最终落点及网络权威边界。

现有转储证明 Antigrav 不是 Buff，也不是 Tek Gravity Grenade；它是 Wasteland ItemTraits 下的“可应用物品 + Trait UObject”双资产。当前仍缺的是 Trait CDO 的 `Modifiers[]` 展开值和原生发射函数内的消费点；我正沿 `GetProjectile*Modifier`、`Server/ClientsFireProjectile` 与瞄准 hook 的参数流补齐。

游戏当前正在运行，因此可以用仓库现有诊断器同样的 `PROCESS_VM_READ` 路径做一次纯 `ReadProcessMemory` 检查，不调用游戏函数、不写进程或文件。我将直接读取 Antigrav 三档 Trait CDO 的 `Name/描述/Modifiers/Buffs`，这是确认 50% 落点最可靠的现有证据。

纯内存检查显示当前地图未实例化 Antigrav CDO，但全局 NamePool 保留了完整三档资产、默认对象与 modifier 标识；同时已定位原生实现入口：`GetProjectileGravityModifier → ArkAscended+0x70A1940`，`GetProjectileSpeedModifier → +0x70A0FE0`。现在直接只读反汇编这两个 native body，确认它们查询哪个 Trait/Modifier 以及返回公式。

已确认两个函数都从 `AShooterWeapon::AssociatedPrimalItem`（武器关联的物品实例）读取 `MyItemTraits`，以 Trait 名 `Antigrav` 分别取 modifier 索引 0（弹速）和 1（重力），缺失时返回 1.0；没有通过 Buff 或技能。最后再查这两个返回值在原生发射路径中的引用，确认是否写入 projectile movement 实例字段。

可以，已经定位到原生实现，不需要从投射物移动组件里盲目找。

### “反重力”特性的真实调用链

特性名称为：

```text
PrimalItemTrait_Projectile_Antigrav_C
Trait FName: Antigrav
```

武器通过关联物品读取这个特性：

```text
AShooterWeapon::AssociatedPrimalItem
  → UPrimalItem::HasItemTrait("Antigrav")
  → UPrimalItem::GetItemTraitModifier("Antigrav", ModifierIndex)
```

最终由两个原生函数消费：

```cpp
AShooterWeapon_Projectile::GetProjectileSpeedModifier();   // ModifierIndex = 0
AShooterWeapon_Projectile::GetProjectileGravityModifier(); // ModifierIndex = 1
```

完整等级“反重力”的结果就是：

```text
投射物速度倍率 = 1.5
重力倍率       = 0.5
```

注意“减少 50% 下坠”是把重力乘以 `0.5`，不是减去 `0.5`。

### 可以修改的位置

最合适的是 hook 这两个 getter，而不是修改全局 Projectile CDO：

```cpp
float GetProjectileSpeedModifier(...)
{
    const float Native = Original(...);
    return Native * ConfiguredSpeedMultiplier;
}

float GetProjectileGravityModifier(...)
{
    const float Native = Original(...);
    return Native * ConfiguredGravityMultiplier;
}
```

这样能继续使用游戏原生的生成、碰撞、命中和伤害流程。还可以限定只处理带 `Antigrav` 特性的武器，避免影响全部投射物武器。

不建议直接修改：

```cpp
ProjectileClass->ClassDefaultObject->MovementComp
```

因为这会污染同弹种的所有武器，而且修改 `InitialSpeed` 后，已经生成的投射物当前 `Velocity` 不一定同步变化。

### 当前自动预判已经支持该特性

现有 `ProjectileAim.inl` 已经调用：

```cpp
Speed *= ProjectileWeapon->GetProjectileSpeedModifier();

Gravity = WorldGravity
    * Default->MovementComp->ProjectileGravityScale
    * ProjectileWeapon->GetProjectileGravityModifier();
```

所以装备原生反重力特性时，自动预判已经会读取 `1.5x` 弹速和 `0.5x` 重力。不能再手工重复乘一次，否则会变成：

```text
速度：1.5 × 1.5 = 2.25 倍
重力：0.5 × 0.5 = 0.25 倍
```

唯一需要继续实测的是 `ServerFireProjectileEx`：它会直接携带一个 `Speed` 参数，当前预判会优先使用该参数。需要捕获一次带反重力武器的 RPC，确认该值是否已经包含 1.5 倍，避免遗漏或重复计算。

### 服务器限制

与泰克狙击枪类似，实际轨迹仍有 authority 边界：

- **单机/本地主机**：hook 两个 getter 可以改变真实投射物。
- **自己控制的独立服务器**：服务端安装相同 hook，可以改变服务器权威轨迹并保持真实伤害。
- **普通远程服务器**：客户端修改 getter通常只能改变本地预测或视觉。服务器仍可能按自己的速度和重力生成投射物。
- `ServerFireProjectileEx` 携带速度参数，服务器是否信任该值需要实测，不能仅看到本地子弹变快就判定成功。

因此，这两个函数已经可以直接定位和修改，但如果目标是远程服务器上的真实弹速，必须同时观察服务器投射物速度、命中时间和目标伤害。硬编码当前 RVA 不安全，应通过 `UFunction`/特征签名定位并校验函数体。

---

**User:**

尝试一下，可以开启本地本地服务器进行验证E:\asa-localserver

---

**Assistant:**

我会先检查当前未提交改动和现有 hook 基础设施，再选择最小可验证路径：优先修改 `ServerFireProjectileEx/ClientsFireProjectileEx` 的原生 `Speed` 参数并同步预判；如果该武器走无 `Speed` 参数的普通 RPC，再评估 getter 原生 detour。随后启动 `E:\asa-localserver` 的独立测试服，用服务器侧投射物速度/命中时间判断是否真正生效，而不是只看客户端动画。

现有基础设施支持两种方案：ProcessEvent 参数改写，以及经过指令签名校验的原生 getter detour。为了不把当前版本的 RVA 硬编码进功能，我正在读取当前客户端这两个 UFunction 的 Exec 包装器和真实目标指令，随后会按现有霰弹枪 hook 的方式从包装器解析目标并校验。

已确认当前版本两个 getter 的真实 native body，并且它们的 Exec 包装器结构稳定：速度包装器调用 `0x70A0FE0`，重力包装器调用 `0x70A1940`，两者只在 `ModifierIndex=0/1` 上不同。我会从包装器的相对 `call` 动态解析目标，并校验 `AssociatedPrimalItem(0x710)` 与索引指令，不硬编码 RVA。

原生 getter hook 已加入，并带当前版本结构校验；配置默认关闭，开启后默认额外 `1.5×` 弹速、`0.5×` 重力，自动被现有弹道预判读取。现在先做静态诊断，并清理上一轮移除功能后可能残留的符号，再构建。

静态检查发现并清理了上一轮遗留的直接传服列表分支；现在只保留真实方尖碑流程。新增 hook 的包装器和 native body 签名与当前运行进程完全匹配。接下来运行功能扩展、弹道/配置、界面渲染回归，然后做 Release 重建。

回归和 Release 重建均通过。现在启动已有的隔离测试服 `PhoenixFeatureTest-20260917`（不会覆盖主服务器存档），并确认无加入密码、端口与日志正常。随后加载新 DLL，检查 getter hook 是否在游戏内成功匹配。

运行时检查发现 getter hook 没有启用，而投射物事件 hook 正常。原因已定位为包装器首条 `push rbx` 在当前编译中使用了带 REX 前缀的 `40 53`，我的校验少算了这个前缀；安全策略因此正确拒绝安装，没有修改游戏行为。现在修正签名并重新构建，然后使用项目自带的安全卸载入口重载 DLL。

修正版已在客户端加载，两个状态均为成功：`ProjectileModifiersOK=1`、`ProjectileShotEventsOK=1`。测试服也已完成启动并已有本地玩家连接。当前自动保存配置显示你已把测试值调到 `5.0×` 弹速、`0.05×` 重力；我会同时采样客户端和服务器中新生成投射物的初速度、当前速度和重力比例，直接判断服务器是否接受。

只读双端采样已准备好。请现在在 `PhoenixFeatureTest` 中用泰克步枪或弩连续射几发，最好先保持当前 `5×/0.05×`，再关闭该功能射几发作对照；采样不会写游戏内存，只读取客户端与服务器新生成投射物的运动组件。

首轮采样工具本身暴露了两个问题，未得到有效轨迹：服务端 `GWorld` RVA 与客户端不同；客户端遇到已销毁的短生命周期 Actor 后重复读取失效地址。游戏功能没有报错。我会让工具从服务端 UObject 注册表动态寻找有效 World，并对每个 Actor 单独容错后重新采样。

---

**User:**

虽然本地动画加速了，但是实际伤害还是没有加速

---

**Assistant:**

收到，这说明当前修改只影响了客户端预测/表现，服务器仍按原始权威参数处理命中。先读取刚才的双端投射物采样，确认服务器收到的是原速度、还是根本没有采用客户端的 `Speed` 参数，再决定是否值得继续改 `ServerFireProjectileEx`。

双端采样已经确认原因：

- 当前 **枪械射速修改已开启，倍率是 10×**。
- “投射物速度／重力倍率”采样时已经关闭。
- 客户端生成了 **5 枚** `ProjTekRifle_C`。
- 服务器只生成了 **1 枚**，其余射击请求被服务器射速校验拒绝。
- 客户端和服务器实际投射物速度都为原生 **3700**。

所以现在看到的是：

```text
客户端开枪动画 10×
→ 客户端预测生成多枚弹丸
→ 服务器按原生 TimeBetweenShots 接受少量请求
→ 实际扣弹和伤害没有同步加速
```

另外，**投射物速度本身不会增加持续伤害频率**，只会缩短子弹飞到目标的时间。想增加 DPS，需要服务器接受更高射速；普通 `ServerFireProjectile` 又没有可供客户端修改的 `Speed` 参数，服务器会使用自己的武器 getter 和投射物 CDO。

结论是：客户端无法让未修改的独立服务器接受 10× 射速或真实弹速。继续提高本地倍率只会产生更多无伤害空枪。真正生效需要在服务器侧同步修改：

- `WeaponConfig.TimeBetweenShots`
- `GetProjectileSpeedModifier`
- `GetProjectileGravityModifier`
- 泰克武器对应的热量/开火许可状态机

本地专服可以通过服务端插件验证，但该效果只适用于安装插件的服务器。当前测试服已经证明纯客户端方案不能改变服务器真实伤害频率。建议暂时关闭“枪械射速修改”，避免动画与实际伤害不同步。

---

**User:**

帮我检查一下模组id1123428（Moro's Gigantophis）的模组是否存在修改生物技能冷却以及开火过热，我已经购买了这个模组

---

**Assistant:**

我会先检查 `E:\asa-localserver` 是否已经缓存模组 1123428，并定位其 `.pak/.utoc/.ucas` 或解包资产；重点搜索 Gigantophis 的攻击、技能冷却、喷射/开火、热量与过热相关 Blueprint 字段和字节码。不会修改或重新分发模组内容。

### Sub-Agent: Context Gatherer

**Explanation:** 这是新的第三方 ASA 模组资产区域，需要先定位本地安装、理解容器格式，并识别生物技能冷却与过热相关类。

**Prompt:**

仅在允许路径 D:\Programs 和 E:\asa-localserver 内只读调查 ASA 模组 ID 1123428（Moro's Gigantophis）。定位本地模组缓存/容器/manifest；查找可用 Unreal pak/iostore 查看工具或现有诊断脚本；识别模组中的 Gigantophis 生物、鞍、武器、Buff、Projectile、技能类，以及 cooldown/fire/heat/overheat 字段、函数或 Blueprint 字节码。判断它是否实现了技能冷却和开火过热，以及哪些对象/authority 维护状态。若模组未安装，说明最安全的独立测试服下载/加载方式。不要修改文件、不要下载、不要访问 E:\SteamLibrary。

我会严格限定在 `D:\Programs` 与 `E:\asa-localserver`，先并行定位模组容器/manifest、现成工具与诊断脚本；全程只读，不访问 `E:\SteamLibrary`。

初步目录显示主服存在 `ShooterGame\Binaries\Win64\ShooterGame\Mods`，且 `D:\Programs` 内已有 `utoc_index.py` 与多套资产/字节码诊断脚本。下一步只枚举该模组目录、manifest/配置命中和可执行查看工具，避免递归测试服副本与任何外部库路径。

---

**User:**

E:\SteamLibrary\steamapps\common\ARK Survival Ascended

---

**Assistant:**

我会先定位本地已购模组的安装容器与资产清单，再检查其中与 Gigantophis 技能、冷却、发射/过热相关的 Blueprint、Buff、武器和字段；如果资产被 IoStore 封装，则优先复用游戏/仓库现有的只读提取工具，不修改模组文件。

### Sub-Agent: Context Gatherer

**Explanation:** 这是一个新的本地 ASA 模组资产分析区域，需要先定位模组文件、封装格式和可能相关的 Blueprint/运行时类。

**Prompt:**

只读调查用户本地安装的 ASA 模组 ID 1123428（Moro's Gigantophis）。优先检查 E:\SteamLibrary\steamapps\common\ARK Survival Ascended 下的模组安装目录，也可使用 D:\Programs\Begeerte-ASA-main 现有诊断工具和 E:\asa-localserver，但不要修改/提取覆盖原文件。目标：1) 定位 1123428 的 .pak/.utoc/.ucas/manifest；2) 枚举可见资产和类名；3) 找出生物技能冷却、攻击/喷射/开火、热量/过热有关的 Blueprint、Buff、Weapon、Projectile、Status 字段或函数；4) 判断模组是否已经包含可复用的冷却或过热修改逻辑；5) 给出证据路径、类名、字段/函数和后续只读提取方法。不要改代码。

**Response:**

只读调查完成；未修改、解包或覆盖任何文件。

1) 容器与 manifest
- 客户端安装目录：`E:\SteamLibrary\steamapps\common\ARK Survival Ascended\ShooterGame\Binaries\Win64\ShooterGame\Mods\83374\1123428_8453079\Moros_Gigantophis`
- `.pak`：`Content\Paks\Windows\Moros_GigantophisShooterGame-Windows.pak`，68,750 bytes。
- `.utoc`：同目录 `Moros_GigantophisShooterGame-Windows.utoc`，300,416 bytes。
- `.ucas`：同目录 `Moros_GigantophisShooterGame-Windows.ucas`，430,572,432 bytes。
- manifest：`Manifest_UFSFiles_Win64.txt`（297,273 bytes，2,232 行，列出 981 个 `.uasset`）和 `Manifest_NonUFSFiles_Win64.txt`（87 bytes）。没有单独 `.manifest` 扩展名文件。
- 插件描述：`Moros_Gigantophis.uplugin`。保存设置还存在于 `...\ShooterGame\Saved\SaveGames\MorosGigantophis\Settings.sav`。
- `E:\asa-localserver` 当前没有 1123428/Moros_Gigantophis 的服务器容器副本。

2) 可见资产与生成类（`_C` 为 cooked Blueprint 通常对应的生成类名；这是基于资产命名的高可信推断，尚未由当前版本运行时反射确认）
- 主角色/状态：`MorosGigantophis_Character_BP_C`、`DinoCharacterStatusComponent_BP_Gigantophis_C`、`DinoCharacterStatusComponent_BP_Gigantophis_SheddedSkin_C`。
- 技能接口/状态：`Interface_Gigantophis_Constriction__C`、`AN_SetChangingStance_C`；索引另见 `Gigantophis_StanceState.uasset`、Cobra/Coiled/Grab/Gun/Python stance HUD、bite/tail slam/tail swipe/constrict/grab/swallow/hypnosis 等动画与提示资源。
- 冷却：`Constrict_Cooldown_Buff_gigantophis_C`、`Grab_Cooldown_Buff_gigantophis_C`。
- 攻击/控制 Buff：`Buff_ConstrictingDino_C`、`Buff_GrabbedDino_PreventAttack_C`、`Buff_Bloodrage_Gigantophis_C`、`Buff_TailBloodRage_Attacked_C`、`Buff_Gigantophis_VictimHypnotised_C`、`Buff_HypnoKnockoutVic_C`。
- Gatling：`Buff_DinoGigantophis_GattlingGun_C`、`Buff_Gigantophis_GattlingGun_Base_C`、`PrimalItemWeapon_Gigantophis_GattlingGun_C`、`PrimalItemWeapon_Gigantophis_GattlingGun_Parent_C`、`Projectile_Gigantophis_Gattling_C`。
- Pulse：`Buff_DinoGigantophis_PulseGun_C`、`Buff_Gigantophis_PulseGun_Base_C`、`PrimalItemWeapon_Gigantophis_PulseGun_C`、`Projectile_GigantophisPulseGun_C`。
- 开火/过热可见资源：`SingleFire_Gattling.uasset`、`GattlingGun_FireLoop.uasset`、`GattlingGun_SteamOverheat.uasset`、`s_minigun_overheat.uasset`、`P_minigun_heatup_gigant_Converted.uasset`、`ToonGuns_BaseHeat.uasset`、`PulseGun_low_PulseGun_OverHeat.uasset`、`MI_HeatEplosion_Gigant.uasset`。注意 `gigantophis_heat_vision` 是热成像/捕食者视觉语义，不应当作武器热量证据。
- 其他显著资产：`Moros_GigantophisShooterWeaponDB.uasset`、`PrimalGameData_BP_Gigantophis.uasset`、`ModDataAsset_Gigantophis.uasset`、`StarkWars_TestMapArea.umap`。完整 981 项清单直接在 UFS manifest 中。

3) 字段/函数证据边界
- 静态包索引能证明以上 Package/Blueprint/Buff/Weapon/Projectile/Status 名称，但 `.ucas` payload 使用 IoStore 压缩；本机未找到 UnrealPak/IoStore/FModel/CUE4Parse/UAssetAPI 工具，因此本轮不能可靠列出这些 Blueprint 内部的 reflected 字段、函数、默认值或图节点。
- 当前 `ArkAscended.exe`（PID 9476）可只读打开，但 Begeerte 诊断脚本中的三个候选 `GNames/GObjects` 偏移均已随游戏版本失效；为避免把随机内存误报成字段/函数，已停止运行时解析。
- 因而 `AddHeat`、`BPFireWeapon`、`FireInterval`、`LastFireTime`、`CanFire`、`OverHeat`、`Cooldown` 等只能作为下一步查询词，不能声称已在 Moro 模组内部得到验证。
- `D:\Programs\Begeerte-ASA-main\src\internal\Hack\TekHeatRules.h` 确实已有一个可复用的“验证 `BPFireWeapon` 字节码是否仅调用 `AddHeat`”判定器，但它针对既有 Tek 武器诊断，不是 1123428 自带代码，不能作为 Moro 模组包含 `AddHeat` 的证据。

4) 是否包含可复用逻辑
- 冷却：**是，结构证据强**。两个独立、技能专用的 Cooldown Buff 明确说明模组已实现 Constrict/Grab 冷却状态。它们至少可作为复用设计样本；是否有公开持续时间字段、通用父类或仅在角色图中硬编码，需属性/字节码导出后确认。
- 过热：**存在完整机制的可能性很高，但“可复用修改逻辑”尚不能确认**。两套枪有 Base Buff + Dino Buff + Weapon + Projectile 分层，并有 heatup/overheat 声音、材质和纹理；最可能的逻辑宿主是 `Buff_Gigantophis_*Gun_Base_C` 或 `MorosGigantophis_Character_BP_C`。但未看到命名为 Overheat/Cooldown 的独立枪械 Buff，当前证据也可能只覆盖表现资源，所以不能断言已有通用 `AddHeat/Overheat` 函数。

5) 后续只读方法
- 首选静态：使用匹配当前 ASA/UE 版本的 FModel 或 CUE4Parse，直接挂载 `.utoc`（同目录 `.ucas` payload 和 `.pak`），只做 package list/JSON property dump；不要原位提取，输出到新的调查目录。优先查看上述 14 个生成类，导出 class hierarchy、CDO properties、functions 与 Blueprint bytecode references。
- 查询字段/函数关键词：`Cooldown`, `Duration`, `Constrict`, `Grab`, `Attack`, `PreventAttack`, `Fire`, `Shoot`, `BPFireWeapon`, `FireInterval`, `LastFireTime`, `CanFire`, `Ammo`, `Projectile`, `Heat`, `OverHeat`, `AddHeat`, `HeatPerShot`, `Cooling`, `Reload`, `Timer`。
- 运行时备选：先为当前 `ArkAscended.exe` 更新并验证 `GNames/GObjects` 签名（必须以 `name(0)==None`、合理对象数和已知原生类交叉验证），然后复用 `inspect_tekbow.py` 的 UClass 字段链/UFunction 链读取方式，改为 stdout-only、只用 `OpenProcess(0x410)+ReadProcessMemory`，禁止写 JSON/bin。对目标类记录字段类型/offset、函数 `script_num`，再以 `blueprint_property_refs.py` 的引用映射方法检查冷却/热量属性引用。


我会先只读定位模组容器与现有诊断工具，再用工具读取容器索引/字符串；不会解包、覆盖或修改任何文件。

现有目录浏览器对 Steam 安装根目录报工作区访问限制，但 PowerShell 只读查询仍可用于定位。诊断项目内已有运行时对象/属性枚举能力，我现在同时精确查找 1123428 容器与可复用的只读检查入口。

已确认客户端模组目录为 `1123428_8453079\Moros_Gigantophis`，包含完整 `.pak/.utoc/.ucas` 和两份打包 manifest；本地服务器未发现该 ID 的副本。下一步只读筛选 manifest 资产路径，并从小型索引文件提取可见名称，重点查冷却、攻击、喷射、武器、投射物、热量与状态相关标识。

静态证据已精确到 manifest 行：角色、Status 组件、两个显式冷却 Buff、两套枪械 Buff/ItemWeapon/Projectile，以及多项 OverHeat/Heat 资源均存在。由于客户端当前正在运行，我再做一次纯 `ReadProcessMemory` 枚举（不注入、不写文件），尝试确认这些生成类实际暴露的字段、函数和 Blueprint 字节码大小。

运行中客户端可正常只读打开，但现有诊断脚本保存的 `GNames/GObjects` 偏移已随当前游戏版本失效，因此没有安全地强行解析内存。静态容器证据已经足够判断资产结构；我只再核对项目当前 SDK 是否记录了新版反射入口，若没有，就把运行时字段/函数列为“需更新签名后验证”，避免误报。

初步证据已经很明确：模组确实包含两个技能专用冷却 Buff（缠绕、抓取），并且两套枪都有独立的 Gun Base Buff、武器和投射物；过热声音/热量材质也存在。不过“存在过热表现资源”不等于存在可修改的 `AddHeat` 状态机。我再检查本机是否已有 IoStore/FModel/CUE4Parse 工具，并对容器中的名称表做一次只读关键词扫描，尽量把结论推进到具体字段/函数。

检查结果如下，模组已正确安装，未修改或解包原文件。

### 模组位置

```text
E:\SteamLibrary\steamapps\common\ARK Survival Ascended\
ShooterGame\Binaries\Win64\ShooterGame\Mods\
83374\1123428_8453079\Moros_Gigantophis
```

主要容器：

```text
Moros_GigantophisShooterGame-Windows.pak
Moros_GigantophisShooterGame-Windows.utoc
Moros_GigantophisShooterGame-Windows.ucas
```

清单包含约 981 个 `.uasset`。

## 1. 生物技能冷却：确定存在

发现两个专用冷却 Buff：

```text
Constrict_Cooldown_Buff_gigantophis_C
Grab_Cooldown_Buff_gigantophis_C
```

其中抓取冷却资产还能看到这些 Blueprint 标识：

```text
ExecuteUbergraph_Grab_Cooldown_gigantophis
BPCustomAllowAddBuff
BPDeactivated
ForCharacter
ForInstigator
ValModifier
```

这说明缠绕和抓取技能的冷却不是单纯动画计时，而是通过“人物身上是否存在对应冷却 Buff”进行控制。

可能的修改方案是：

```text
技能使用
→ 添加 Constrict/Grab Cooldown Buff
→ Buff 存在期间拒绝再次使用
→ Buff 到期或 Deactivate 后恢复
```

适配时应优先查找当前 Gigantophis 身上的：

```cpp
Constrict_Cooldown_Buff_gigantophis_C
Grab_Cooldown_Buff_gigantophis_C
```

然后确认其持续时间字段或服务器 `BPDeactivated` 路径，而不是改通用攻击速度。

## 2. 枪械过热：有表现资源，但尚未发现独立 AddHeat 状态机

模组包含两套枪械逻辑宿主：

```text
Buff_Gigantophis_GattlingGun_Base_C
Buff_DinoGigantophis_GattlingGun_C

Buff_Gigantophis_PulseGun_Base_C
Buff_DinoGigantophis_PulseGun_C
```

以及：

```text
PrimalItemWeapon_Gigantophis_GattlingGun_C
PrimalItemWeapon_Gigantophis_GattlingGun_Parent_C
PrimalItemWeapon_Gigantophis_PulseGun_C
```

存在明确的过热表现资源：

```text
GattlingGun_SteamOverheat
s_minigun_overheat
P_minigun_heatup_gigant_Converted
PulseGun_low_PulseGun_OverHeat
ToonGuns_BaseHeat
MI_HeatEplosion_Gigant
```

但对容器可见名称进行扫描后，没有发现：

```text
AddHeat
OverheatingAmount
HeatPerShot
GetIsOverheated
OverheatDuration
```

因此目前的结论是：

- 模组肯定有枪械升温/蒸汽/发光等过热表现。
- 不能确认它具有类似泰克狙击枪的独立 `AddHeat` 状态机。
- “过热”可能实际上由弹药、重装、Buff 状态或角色 Blueprint 中的计时器控制。
- 最可能的真实逻辑位置是两个 `Buff_Gigantophis_*Gun_Base_C`，而不是武器类本身。

## 可修改性判断

| 功能 | 结论 | 优先检查对象 |
|---|---|---|
| 缠绕冷却 | 确定存在 | `Constrict_Cooldown_Buff_gigantophis_C` |
| 抓取冷却 | 确定存在 | `Grab_Cooldown_Buff_gigantophis_C` |
| 加特林过热 | 有表现证据，状态机未确认 | `Buff_Gigantophis_GattlingGun_Base_C` |
| 脉冲枪过热 | 有表现证据，状态机未确认 | `Buff_Gigantophis_PulseGun_Base_C` |
| 通用 `AddHeat` | 未发现 | 可能没有使用该模式 |

这些冷却和开火判定很可能由服务器 authority 管理。普通远程服上仅删除客户端 Buff 或清除本地变量，可能只会让按键/动画恢复，服务器仍会拒绝技能和伤害。下一步需要让本地测试服加载模组 1123428，再通过运行时反射读取上述四个类的字段、函数和 Blueprint 字节码，才能确认过热究竟是热量值、计时器、弹药还是 Buff 门控。

---

**User:**

好的，尝试一下

---

**Assistant:**

我会把 1123428 以可逆的目录联接挂到隔离测试服，并只修改该测试实例的启动参数；先优雅保存并停止 `PhoenixFeatureTest`，确认 ASA 的本地模组参数格式，再重启后检查服务器日志是否真正挂载 `Moros_Gigantophis`。主服务器和原模组文件不会改动。

已确认 ASA 使用 `-mods=1123428`，测试实例拥有独立的 `ShooterGame\Mods` 目录，因此不会污染其他服务器。现在让测试服通过官方 CurseForge Core 安装/挂载该模组；首次启动可能需要下载约 430 MB。

模组已由服务器官方下载器成功安装并作为 `PrimalGameDataOverride` 加载，测试服已开放且客户端已连接。下一步直接从服务器进程反射目标类，读取 CDO 字段、函数 flags、参数布局和 Blueprint 字节码引用；这能区分“纯表现过热”与真正的服务器门控。

反射结果已确认两点：技能冷却由 Buff 的 `DeactivateAfterTime` 驱动；枪械过热不是纯表现，服务器角色类确实有复制状态 `Overheating`、`Current Heat %` 和 `OnRep_Overheated`。我正在把这些字段反向映射到具体 Blueprint 函数，找出增热、降热、拒绝开火和重装的入口。

已成功让本地测试服加载模组 1123428，并从**服务器进程**读取了 Blueprint 类、字段和字节码。测试服仍在运行：

```text
open 127.0.0.1:7797
服务器：PhoenixGigantophisTest
无加入密码
```

## 检查结论

### 1. 缠绕冷却：模组原生支持配置

服务器加载了：

```text
Constrict_Cooldown_Buff_gigantophis_C
```

其默认值为：

```text
DeactivateAfterTime = 60.0 秒
```

模组管理员设置中还明确存在：

```text
Cooldown for being able to constrict the same target again. (Seconds)
```

调用链为：

```text
管理员设置
→ MorosGigantophis_CCA_C::ApplySettingsToCDOs
→ 修改冷却 Buff/CDO
→ BPCustomAllowAddBuff 检查重复 Buff
→ 60 秒后 BPDeactivated
```

因此，同一目标的缠绕冷却可以直接通过模组自己的管理员设置修改，不必额外 hook。设为 `0` 或较低值即可测试。

### 2. 抓取冷却：存在独立 Buff，但没有发现管理员配置项

资产中存在：

```text
Grab_Cooldown_Buff_gigantophis_C
```

但当前服务器尚未加载这个类，说明它可能在第一次使用抓取技能时才延迟加载。53 项模组管理员设置中也没有发现“Grab Cooldown”。

需要在游戏里实际使用一次抓取技能后，再读取：

```text
Grab_Cooldown_Buff_gigantophis_C::DeactivateAfterTime
```

才能确认默认时间和修改入口。

### 3. 枪械过热：确认是真实服务器状态机

服务器角色类中确认存在：

```cpp
bool   Overheating;        // offset 12536
double Current Heat %;     // offset 12544
bool   Gun Is Doing Rapidfire;
bool   Play Gun Reload;
```

相关函数包括：

```text
Set Heat For Gun
OnRep_Overheated
RapidFire Right Click ROS
RapidFire Right Click Release ROS
Input Action Fire ROS
BPTimerServer
```

字节码显示热量使用 `FInterpTo_Constant` 更新：

```text
持续射击目标值：1.0
停止射击目标值：0.0
升温/降温速率常量：约 0.13 / 0.10
达到 1.0：Overheating = true
并停止 Gun Is Doing Rapidfire
```

还发现约 `0.9` 的表现阈值，可能用于提前改变枪体颜色或播放蒸汽效果。

这证明过热不是单纯动画，而是 `MorosGigantophis_Character_BP_C` 中的真实状态机。

### 4. 模组没有提供关闭过热的管理员选项

模组的 53 项设置包括：

- 加特林/脉冲枪基础伤害
- 枪械伤害倍率
- 是否按近战伤害缩放
- 缠绕同一目标的冷却
- 抓取、吞咽、催眠等规则

但没有：

```text
Disable Gun Overheat
Heat Rate
Cooling Rate
Overheat Duration
```

所以枪械过热不能通过模组现有管理员菜单直接关闭。

## 可实施方案

- **缠绕冷却**：直接使用模组管理员菜单把同目标冷却设为 0，属于模组原生支持。
- **抓取冷却**：先使用一次抓取让类加载，再读取并调整 `DeactivateAfterTime`。
- **枪械不过热**：需要服务器侧持续维护：
  ```cpp
  Current Heat % = 0.0;
  Overheating = false;
  ```
  或拦截 `Set Heat For Gun` 的 Blueprint 入口。只改客户端会再次出现动画正常、服务器拒绝攻击的问题。

请进入测试服，生成/骑乘 Gigantophis，然后分别使用一次抓取和持续开枪。我就可以继续读取活动 Buff 的真实持续时间，以及服务器热量随射击变化的曲线。

模组身份也与 [Moro's Gigantophis 官方 CurseForge 页面](https://www.curseforge.com/ark-survival-ascended/mods/moros-gigantophis)一致。网页内容已按许可要求改写。

---

**User:**

抓取没有冷却，只需要修改开枪的热量和射速即可

---

**Assistant:**

了解，范围缩小为 Gigantophis 枪械：清除服务器热量状态并调整服务器实际射击间隔，不处理抓取。由于 `Input Action Fire ROS` 无速度/间隔参数，伤害由服务器角色状态机决定，我不会再做只加速客户端动画的方案；先复用本机 ASA API 插件模板，做最小的 authority 侧实现并装到隔离测试服验证。

### Sub-Agent: Context Gatherer

**Explanation:** Gigantophis 的真实热量和射速由独立服务器 authority 管理，需要在陌生的 ASA API 服务端插件区域选择正确模板、生命周期和反射访问方式。

**Prompt:**

调查 D:\Programs 中现有 ASA API 服务端插件项目（优先 ASA-CustomRecipeStack-ServerPlugin、ASA-ClusterResourceTransfer）和 E:\asa-localserver 的 ArkApi/AsaApiLoader 环境，为 D:\Programs\Begeerte-ASA-main 设计一个最小 Gigantophis 服务端伴侣插件。目标：仅当配置开启且对象类链为 MorosGigantophis_Character_BP_C 时，在服务器 authority 上将字段 `Current Heat %` 置 0、`Overheating` 置 false，并找到/调整真正服务器射速门控（优先分析 runtime dump D:\Programs\Begeerte-ASA-main\release\diagnostics\gigantophis-server-runtime 中 Last Attack Times、Gun Is Doing Rapidfire、Input Action Fire ROS、Set Heat For Gun、BPTimerServer 和字节码证据）。请返回：1) 可复用插件模板与构建命令；2) 安全的加载/卸载/tick hook API；3) SDK/反射字段访问方式；4) 射速最小可行修改点；5) 安装到 PhoenixFeatureTest 的路径；6) 风险与验证方法。不要改文件。

**Response:**

调查结论：不需要、也不应先 hook Gigantophis 的 Blueprint 函数。最小伴侣插件应只注册 AsaApi 的 game-thread timer，动态解析目标 Blueprint 类，扫描该类 actor，并且仅在配置开启、类链包含 `MorosGigantophis_Character_BP_C`、`actor->HasAuthority()` 时写入热量字段。未修改任何文件。

1) 可复用模板与构建
- 建议目录：`D:\Programs\Begeerte-ASA-main\server-plugin\GigantophisServerCompanion\`，内容最小化为 `src/Main.cpp`、`config.json`、`PluginInfo.json`、`.vcxproj/.sln`、`tools/build.ps1`。
- 直接复用 `ASA-CustomRecipeStack-ServerPlugin` 的工程参数：x64/Release DLL、C++20、`/MD`、`v145`、包含 `extern\AsaApi\AsaApi\Core\Public` 与 `API\UE`，链接 `AsaApi.lib`、`fmt.lib`。输出目录设为 `dist\GigantophisServerCompanion\`，构建后复制 config/PluginInfo。
- 构建命令：`powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -PlatformToolset v145`；等价直接命令为 `MSBuild.exe GigantophisServerCompanion.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /m /v:minimal /nologo`。
- 配置建议默认关闭：`{"Enabled":false,"DisableHeat":true,"RapidFireIntervalSeconds":0.2,"EnableFireRateOverride":false}`。射速覆盖必须作为第二个、独立且默认 false 的开关。

2) 安全生命周期/tick API
- 最安全 API 是现有项目已验证的 `AsaApi::GetCommands().AddOnTimerCallback(L"GigantophisServerCompanion.Tick", &Tick)`；卸载顺序：先 `active=false`，再 `RemoveOnTimerCallback`，然后通过一次新鲜 actor 扫描恢复曾改动的 BPTimer interval，最后清空弱引用/状态。timer 在游戏线程运行，避免后台线程碰 UObject。
- `Plugin_Init`：初始化日志、按 DLL 所在目录读取 config；配置关闭时不注册 timer；注册失败应抛错且调用统一 Cleanup。`Plugin_Unload` 必须幂等。
- 不缓存裸 actor 指针跨 tick；每次 `UGameplayStatics::GetAllActorsOfClass(world, TSubclassOf<AActor>{targetClass}, &actors)`，或只缓存 `FWeakObjectPtr`。检查 world、actor、pending-kill/有效性、authority，并限制数组数量。
- 如果未来确需 native hook，严格使用现有的 `SetHook`/`DisableHook` 成对模式、记录每个成功安装项、反向卸载；但本需求无需 hook。尤其不要对下列 Blueprint UFunction 调 `SetHook`：runtime dump 显示它们的 `exec` 全是共享 VM thunk `0x13c6c90`，hook 该地址可能波及大量 Blueprint。

3) SDK/反射字段访问
- 目标包路径来自 runtime outer：`/Moros_Gigantophis/Dinos/Gigantophis/MorosGigantophis_Character_BP`。world ready 后可用 `UVictoryCore::BPLoadClass(FString("Blueprint'/Moros_Gigantophis/Dinos/Gigantophis/MorosGigantophis_Character_BP.MorosGigantophis_Character_BP'"))`；若已加载，也可用 `Globals::StaticFindObject` 找生成类。解析后必须验证 `NamePrivateField()=="MorosGigantophis_Character_BP_C"`，不要保存 dump 中地址/偏移。
- 类条件建议同时做两层：`actor->IsA(targetClass)`，并沿 `actor->ClassField()->SuperStructField()` 最多 64 层确认生成类名精确等于 `MorosGigantophis_Character_BP_C`。这复用了 ClusterResourceTransfer 的 ancestry 模式，可防路径误解析。
- `Current Heat %` 是 `DoubleProperty`，runtime offset 12544/0x3100；使用动态访问而非偏移：`*GetNativePointerField<double*>(actor, "MorosGigantophis_Character_BP_C.Current Heat %") = 0.0;`。
- `Overheating` 是 BoolProperty，offset 12536/0x30F8，dump 的 bool 布局为 `[size=1, byteOffset=0, byteMask=1, fieldMask=255]`。最稳妥写法是 `SetNativeBitField<bool, uint8_t>(actor, "MorosGigantophis_Character_BP_C.Overheating", false)`，不要假设所有 UE bool 都能当普通 `bool*`。
- 写前读值，只在非零/true 时写；任何字段解析异常应将运行功能熔断并只记一次错误。可按需在发生变化时 `ForceNetUpdate`，但不要每 tick 调；是否需要由客户端 OnRep 验证决定。
- 核心伪代码：`if (enabled && actor && actor->HasAuthority() && ClassChainMatches(actor)) { auto* heat=GetNativePointerField<double*>(...); if (*heat!=0) *heat=0; SetNativeBitField<bool,uint8_t>(...,false); }`。

4) 服务器射速最小可行修改点
字节码证据表明“清热”本身不会提高射速，真正链路是两层：
- `Input Action Fire ROS`、`Set Heat For Gun`、`BPTimerServer` 都只是 18-byte Kismet thunk，均跳转同一 `ExecuteUbergraph_MorosGigantophis_Character_BP`，入口分别约为 `0x167AF`、`0x166EA`、`0xDED7`；地址仅用于本次分析，绝不能硬编码。
- `BPTimerServer` 对应 ubergraph 区域直接引用 `Gun Is Doing Rapidfire`（约 `+0xE043`）并紧邻调用 `ShootGun FullAuto` delegate（约 `+0xE056`）；同一区域在 `+0xE4B1..+0xE504` 更新 `Current Heat %`，在 `+0xE518/+0xE52F` 更新 `Overheating`。捕获的目标默认值还显示 `BPTimerServerMin == BPTimerServerMax == 0.2`。因此 strongest-evidence 的服务器连发周期驱动是目标 actor 的 BPTimer cadence。
- 另有二级攻击资格门：`CentralCanAttack.bin` 是 11086-byte 实际逻辑，直接引用 `Last Attack Times`（bin `+0x2C4`），其局部变量中有多个 `CallFunc_IsTimeSince_ReturnValue_*`；`BlueprintCanAttack` 只是 83-byte 转发到 CentralCanAttack。这说明 `Last Attack Times` 是通用攻击 cooldown/资格状态，但当前证据没有可靠确定“枪”使用的数组索引。
- 最小可行射速修改应先仅在 `Gun Is Doing Rapidfire==true` 的目标 authority actor 上，将继承字段 `APrimalCharacter.BPTimerServerMin` 和 `BPTimerServerMax` 动态设为配置 interval（建议初始只测 0.15/0.10，并硬 clamp 0.05–0.20）；退出 rapidfire、配置关闭、卸载时恢复该 actor 原值。不要改 CDO，这会影响新实例/其他系统。
- BPTimer 还处理盘绕/姿态/热量等逻辑，因此这是“可行但有副作用”的点。先用 10 秒 projectile/ammo 计数确认频率随 interval 改变。如果仍被封顶，再观测真实 actor 的 `Last Attack Times` 在射击前后哪个索引变化，只回拨那个枪索引；绝不能清空整个数组，也不能改所有元素，因为 CentralCanAttack 同时服务至少攻击索引 0,1,2,3,4,6,7,8,10,12,15,16,19。当前 dump 的默认对象该数组为 num=0，不足以推断枪索引。
- 不建议直接 hook `Input Action Fire ROS`/`BPTimerServer`/`CentralCanAttack`：它们共享 Blueprint VM exec，不是独立 native hook 点。

5) PhoenixFeatureTest 安装路径
- 最终插件目录应为：`E:\asa-localserver\PhoenixFeatureTest-20260917\ShooterGame\Binaries\Win64\ArkApi\Plugins\GigantophisServerCompanion\`，内含同名 DLL、`PluginInfo.json`、`config.json`。
- 当前 PhoenixFeatureTest 的私有 `Win64` 中没有 `ArkApi` 目录，也没有 `AsaApiLoader.exe`；只有 Content/Plugins 是指向主服务端的 junction。因此它现在不能仅靠复制插件加载。
- 隔离测试推荐把主安装 `E:\asa-localserver\ShooterGame\Binaries\Win64\AsaApiLoader.exe` 与完整 `ArkApi` runtime 复制到 Phoenix 的私有 Win64，再只放本插件；不要把 Phoenix 的 ArkApi junction 到主 `ArkApi`，否则主目录现有所有插件都会一起加载。启动时工作目录必须是 Phoenix 的 Win64，并用 `AsaApiLoader.exe ...`，不是直接运行 `ArkAscendedServer.exe`。

6) 风险与验证
- 版本漂移：mod 更新可能改类名、字段名/类型、BPTimer graph；启动时验证类名、字段可解析、double/bool 语义，并 fail closed。绝不使用本次 dump 的 UObject 地址或固定 offset。
- authority/复制：只写 authority；观察客户端热量 HUD、`OnRep_Overheated` 和实际服务器能否继续发射。原始内存写可能不会立刻 dirty replication；只在值变化时测试 `ForceNetUpdate`。
- timer 副作用：降低 BPTimer interval 会加速该 Blueprint 中非射击逻辑并增加 CPU。逐级测试 0.20→0.15→0.10，监测 server frame、姿态/盘绕、AI turret、弹药消耗和 projectile 数；禁止默认低于 0.05。
- 卸载：热量值无需恢复（目标就是清零），但 fire interval 必须恢复。卸载前停止 callback，使用 fresh scan/weak refs，绝不解引用陈旧 actor。
- 验证矩阵：配置 false（零写入）；非目标 dino（零写入）；目标非 authority/客户端（零写入）；目标 authority 单发/连发；过热后立刻恢复；骑乘和 AI turret；stasis/uncryo；热重载/卸载；服务器重启。用现有只读 `inspect_feature_runtime.py` 复抓字段，并以服务端 projectile 数与 ammo delta/10 秒作为射速真值，不以客户端动画/HUD 为准。
- 建议分两阶段上线：第一阶段只启用 DisableHeat；第二阶段在专用测试服单独打开 EnableFireRateOverride。

我先并行锁定三类证据：现有插件的生命周期/构建模板、ArkApi/AsaApiLoader 的实际安装布局，以及 Gigantophis runtime dump 中热量与射速变量的引用位置。随后只读取命中的关键片段，不修改任何文件。

已确认可复用模板采用 `Plugin_Init/Plugin_Unload`、`AddOnTimerCallback/RemoveOnTimerCallback` 和成对 `SetHook/DisableHook`，本地加载器位于主实例 `ShooterGame\Binaries\Win64`。下一步集中解析 Gigantophis 类元数据与 4 个关键字节码函数，并核对 PhoenixFeatureTest 的 junction/插件落点。

关键差异已经明确：`Set Heat For Gun` 和 `BPTimerServer` 都只有 18 字节脚本，极可能只是转发/委托入口；`Input Action Fire ROS` 是服务器 RPC（flags 含 Net/Server），而 `BlueprintCanAttack` 才有 83 字节实际判定逻辑。现在补齐字节码指针交叉引用和 ArkApi 的对象遍历/字段 helper 定义，即可给出最小修改点及保守回退方案。

将新增一个最小独立伴侣插件，不改客户端主 DLL：

```text
server-plugin/GigantophisServerCompanion/
├─ src/Main.cpp          # authority tick：清热 + 连发时调整 BPTimer
├─ config.json           # 默认先清热，射速 0.15s
├─ PluginInfo.json
├─ GigantophisServerCompanion.vcxproj/.sln
└─ tools/build.ps1
```

它动态按类名/字段名解析，不使用本次转储地址；卸载时恢复 `BPTimerServerMin/Max`。依赖复用本机现有 ASA API SDK，仅打包插件 DLL 和配置。

---
