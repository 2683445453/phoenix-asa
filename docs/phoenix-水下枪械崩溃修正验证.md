# 2026-09-16 水下枪械崩溃修正验证

本次修正针对开启水下枪械后，扫描背包武器模板时发生的非法内存读取。代码和离线测试已补充访问检查；**尚未重新注入游戏，也未复现验证实际水下装备、开火及服务器反馈。**

[原始崩溃记录](D:/Programs/Begeerte-ASA-main/release/water-crash/before/09.16-19.19.33.crashstack)显示：ASA 93.24、CL 829680，Ragnarok_WP 客户端，启用 134 个模组，其中包含 QoL+。异常为读取 `0x0000001000000010`，栈顶是旧 `BackpackTools.inl:80`，上层为 `hkPostRender`。该路径先通过 `Item->WeaponTemplate.Get()` 取得类，再读取类默认对象（CDO）；第 80 行检查类并读取 CDO，下一步直接检查 CDO 的类型。

旧路径对背包物品、软类引用和默认对象的验证不足：生成的 `.Get()` 不核对弱引用序列号，通用对象检查不能证明结果确实是武器类，也没有先确认 CDO 自身的有效性。过期引用、对象槽复用或解析到非类对象，都可能把后续访问带到无效地址；离线测试覆盖了这些路径。**堆栈没有记录具体物品和引用内容，因此不能断定这次由哪件物品或哪个模组触发。**

当前处理集中在 [WeaponWaterAccess.inl](D:/Programs/Begeerte-ASA-main/src/internal/Hack/WeaponWaterAccess.inl)、[UnderwaterWeapons.inl](D:/Programs/Begeerte-ASA-main/src/internal/Hack/UnderwaterWeapons.inl) 和 [BackpackTools.inl](D:/Programs/Begeerte-ASA-main/src/internal/Hack/BackpackTools.inl)：

- 先验证内存范围、对象注册表身份、物品类型和继承关系；通过运行时反射确认 `WeaponTemplate` 的字段类型、大小及边界，再核对弱引用索引与序列号。无效或未加载的模板直接跳过。
- 确认结果为支持的枪械或泰克相枪类，并校验 CDO 标记、所属类及有效状态。水下开火、装备标志通过反射定位和位掩码检查写入，保留相邻标志。
- 每 250ms 扫描一次。关闭、换武器、目标失效或人物／世界变化时清理本功能的修改；恢复前重新核验身份，仅在当前值仍等于本功能写入值时恢复原值，避免覆盖其他来源的修改。
- 新增武器实例创建时间检查，区分临时修改 CDO 后生成的实例与原本已有的实例，避免把旧武器自身的属性误当成继承值。仅创建时间严格晚于首次修改时间时继承原始默认值；时间未知、为零或不晚于修改时间时，保留观察到的实例原值。

[访问测试日志](D:/Programs/Begeerte-ASA-main/release/water-crash/access-tests.log)已记录通过：测试直接编译生产解析代码并使用实际 SDK 布局，覆盖同类无效地址、空／过期／非类软引用、注册槽复用、零序列号元数据、错误反射类型与边界、CDO 身份、销毁状态及相邻位保护。快捷键主菜单检查，以及背包筛选、本地相机和 Lua 回归日志也已有通过记录。上述均为离线验证，不等同于原服务器场景已通过。

最终离线验证已通过：

- 真实 SDK 布局访问、反射时间与边界检查：`release/water-crash/access-tests.log`。
- 直接执行生产开关流程的生命周期测试，包括旧实例覆盖、新实例继承、未知／相同／零时间、对象与类复用、CDO 延迟序列号、反复开关及原始标志恢复：`release/water-crash/runtime-tests.log`。
- 背包筛选、本地相机和 Lua 回归：`release/water-crash/backpack-lua-camera-tests.log`；主菜单、加载、缺失函数、界面输入及快捷停止：`release/water-crash/shortcut-tests.log`。
- Release x64 构建及启动器本地 DLL 检查：`release/water-crash/build.log`。
- 中英文 646 个对应语言键、443 条界面文本、占位符、Lua 脚本、使用说明、ZIP CRC 及包内 DLL 一致性：`release/water-crash/package-integrity.log`。
- 受控原生性能测试中，500 个资源条目的检查由约 37.8 ms 降至约 7.8 ms；此值不代表实际游戏帧耗时：`release/water-crash/access-benchmark.log`。

无法可靠核对实例身份或模组反射字段的武器会跳过。默认对象用有效所属类核验生命周期，普通武器实例需有效自身序列号；不会猜测对象身份或主动加载模板。未执行实际游戏注入与水下开火复测。

发布包：[phoenix.zip](D:/Programs/Begeerte-ASA-main/dist/phoenix.zip)。DLL SHA256：`128d4d42b74f4f6362bb723ebed5f9211d1b27b44995e2c0c55009f2405d4b5e`。

ZIP SHA256：`bb9c5d34488dc70e61b06383f2ea2041b7d46fe3a2294b2b70520e7d7f67e4da`。

建议按以下顺序进行实机复测，并记录服务器、模组组合、武器名称和触发操作：

1. 完整退出旧游戏后替换新包。先保持水下枪械关闭，进入原模组环境；背包保留资源、皮肤、蓝图、普通枪械、泰克相枪及 QoL+ 物品，确认进入和打开背包稳定。
2. 陆地上开启、关闭数次，增减混合物品、打开容器并重新装备武器，确认扫描不会崩溃，无适用武器时正常显示状态。
3. 分别使用开启前已装备的枪、开启后才装备的枪和泰克相枪入水，检查装备与开火；分别记录动画、实际扣弹和命中伤害，以服务器反馈为准。
4. 切换武器、收回背包或放入容器后关闭功能，确认原本的水下限制恢复；原本允许水下使用的武器应保留其原有行为。
5. 在功能开启时覆盖死亡／重生、断线返回主菜单、重新进入或换图，以及快捷停止，确认不会访问旧武器、旧人物或旧世界对象，关闭后没有残留修改。
