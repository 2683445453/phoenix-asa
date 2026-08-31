// 21 August 2026
class AShooterGameState : public AGameState
{
public:
	uint8                                         Pad_578[0x10];                                      // 内存对齐填充（0x10字节）
	int32                                         NumNPC;                                             // 当前世界中存在的NPC/野生生物总数量
	int32                                         NumHibernatedNPC;                                   // 处于休眠/冻结状态（Stasis）的NPC数量（远离玩家不进行Tick计算）
	int32                                         NPCActiveCountTamed;                                // 当前处于活跃状态（未休眠）的已驯服生物数量
	int32                                         NumActiveNPC;                                       // 当前处于活跃状态（正在执行Tick计算）的NPC/生物总数量
	int32                                         NumDeadNPC;                                         // 已死亡但尚未被垃圾回收/销毁的NPC数量
	int32                                         NumPlayerActors;                                    // 当前世界中已生成的玩家角色Actor数量（包括离线倒地玩家）
	int32                                         NumPlayerConnected;                                 // 当前在线连接的玩家总人数
	bool                                          bServerUseLocalizedChat;                            // 服务器是否启用区域/本地化聊天模式
	bool                                          bServerUseProximityVoiceChat;                       // 服务器是否启用近距离语音对讲模式
	uint8                                         Pad_5A6[0x2];                                       // 内存对齐填充
	float                                         ProximityChatInnerRadius;                           // 附近语音/文字聊天的内圈全音量距离
	float                                         ProximityChatOuterRadius;                           // 附近语音/文字聊天的外圈衰减极限距离
	float                                         LocalizedChatRadiusUnconsiousScale;                 // 玩家处于昏迷状态时，接收区域聊天的范围衰减缩放倍率
	float                                         ServerFramerate;                                    // 服务器端当前运行帧率（Server Tick / SV FPS）
	class FString                                 NewStructureDestructionTag;                         // 新放置建筑销毁机制的事件标识标签
	int32                                         DayNumber;                                          // 游戏内的当前天数（Day计数）
	float                                         DayTime;                                            // 游戏内的当前时间（24小时制浮点数，如14.5表示下午2:30）
	double                                        NetworkTime;                                        // 服务器网络主时间戳（用于客户端与服务端时钟同步）
	uint32                                        TimeUTC;                                            // 当前现实时间的UTC时间戳（秒级）
	int32                                         NetUTC;                                             // 用于网络数据包同步校准的UTC时间戳
	bool                                          bIsOfficialServer;                                  // 当前是否为官方服务器
	bool                                          bIsListenServer;                                    // 当前是否为主机/非专用服（Listen Server）
	bool                                          bIsDediServer;                                      // 当前是否为独立专用服务器（Dedicated Server）
	bool                                          bIsServerRunningOnConsole;                          // 服务器后端是否运行在主机平台（PS/Xbox）
	bool                                          bIsArkTributeAvailable;                             // 是否允许方舟贡品/方舟终端功能
	bool                                          bIsArkDownloadsAllowed;                             // 是否允许从方舟云端下载物品、人物或恐龙
	bool                                          bAllowThirdPersonPlayer;                            // 是否允许玩家开启第三人称视角
	bool                                          bServerHardcore;                                    // 是否为极限生存模式（死后角色直接被删除）
	bool                                          bServerPVE;                                         // 是否为PVE（玩家对环境）模式
	bool                                          bAutoPvE;                                           // 是否开启按时间自动切换PVE/PVP机制
	bool                                          bServerCrosshair;                                   // 服务器是否启用屏幕准星
	bool                                          bServerForceNoHUD;                                  // 服务器是否强制关闭所有玩家的HUD界面
	bool                                          bFlyerPlatformAllowUnalignedDinoBasing;             // 飞行平台鞍是否允许未对齐的生物站立停靠
	bool                                          bMapPlayerLocation;                                 // 打开地图时是否显示“你在地图上的位置（小针）”
	bool                                          bPvEDisableFriendlyFire;                            // PVE模式下是否关闭友军伤害
	bool                                          bPvEAllowTribeWar;                                  // PVE模式下是否允许发起部落战争
	bool                                          bPvEAllowTribeWarCancel;                            // PVE模式下是否允许中途取消部落战争
	bool                                          bEnablePvPGamma;                                    // PVP模式下是否允许玩家调节Gamma亮度参数
	bool                                          bPreventDroppedItemPhysics;                         // 是否禁用掉落包裹/物品的物理碰撞模拟（减少服务器负载）
	bool                                          bDisablePvEGamma;                                   // PVE模式下是否禁止玩家调节Gamma亮度
	int32                                         NumTamedDinos;                                      // 全服当前已驯养生物的总数量
	int32                                         MaxStructuresInRange;                               // 区域范围内允许放置的最大建筑数量（建筑上限）
	float                                         DayCycleSpeedScale;                                 // 昼夜循环整体时间流速倍率
	float                                         DayTimeSpeedScale;                                  // 白天时间流速倍率
	float                                         NightTimeSpeedScale;                                // 黑夜时间流速倍率
	int32                                         StartTimeHour;                                      // 游戏初始化时的起始小时数
	float                                         PvEStructureDecayPeriodMultiplier;                  // PVE模式下建筑自动腐败解体时间倍率
	float                                         PvEDinoDecayPeriodMultiplier;                       // PVE模式下驯服恐龙无人认领/饥饿死亡腐败时间倍率
	float                                         PerPlatformMaxStructuresMultiplier;                 // 平台鞍上最大建筑数量上限倍率
	float                                         ImplantSuicideCD;                                   // 使用植入体自杀的冷却时间（秒）
	bool                                          bDisableStructureDecayPvE;                          // PVE模式下是否禁用建筑自动解体腐蚀机制
	bool                                          bDisableDinoDecayPvE;                               // PVE模式下是否禁用恐龙自动腐化机制
	bool                                          bAllowCaveBuildingPvE;                              // PVE模式下是否允许在洞穴内建造
	bool                                          bAllowCaveBuildingPvP;                              // PVP模式下是否允许在洞穴内建造
	bool                                          bPreventDownloadSurvivors;                          // 是否禁止下载跨服上传的幸存者角色
	bool                                          bReachedPlatformStructureLimit;                     // 全服平台鞍建筑数量是否已触顶达到最大限制
	bool                                          bAdminLogging;                                      // 是否在公屏/日志中播报管理员管理命令
	bool                                          bPvPStructureDecay;                                 // PVP模式下是否启用建筑腐败解体
	bool                                          bPreventDownloadDinos;                              // 是否禁止下载跨服上传的恐龙
	bool                                          bPreventDownloadItems;                              // 是否禁止下载跨服上传的物品
	bool                                          bPreventUploadDinos;                                // 是否禁止将恐龙上传至方舟云端
	bool                                          bPreventUploadItems;                                // 是否禁止将物品上传至方舟云端
	bool                                          bPreventNonClusterCharacterUploadsWithItems;        // 非集群服务器是否禁止角色携带物品上传
	bool                                          bPreventUploadSurvivors;                            // 是否禁止将幸存者角色上传至方舟云端
	bool                                          bPreventMateBoost;                                  // 是否禁用异性生物配偶加成（Mate Boost）
	bool                                          bPreventStructurePainting;                          // 是否禁用建筑染色涂装功能
	bool                                          bAllowCharacterCreation;                            // 服务器是否允许新建角色
	bool                                          bAllowSpawnPointSelection;                          // 死亡后是否允许自由选择复活点/床位
	uint8                                         Pad_62E[0x2];                                       // 内存对齐填充
	int32                                         MaxTamedDinos;                                      // 全服允许存在的最大驯服生物总数上限
	bool                                          bDisableSpawnAnimations;                            // 是否禁用复活/苏醒时的挠手臂动画
	uint8                                         Pad_635[0x3];                                       // 内存对齐填充
	class FString                                 PlayerListString;                                   // 在线玩家列表字符串（通常由逗号或换行符拼接）
	TMulticastInlineDelegate<void(const class FString& PlayerList)> OnPlayerListPopulated;            // 玩家列表获取并填充完成时的多播事件委托
	float                                         GlobalSpoilingTimeMultiplier;                       // 全局食物/肉类腐烂变质时间倍率
	float                                         GlobalItemDecompositionTimeMultiplier;              // 全局丢弃物品掉落包消失分解时间倍率
	int32                                         MaxNumberOfPlayersInTribe;                          // 每个部落允许加入的最大玩家人数上限
	float                                         TribeSlotReuseCooldown;                             // 部落成员退出/被踢后槽位再次加入的冷却时间
	float                                         GlobalCorpseDecompositionTimeMultiplier;            // 玩家死亡尸体/背包消失分解时间倍率
	float                                         EggHatchSpeedMultiplier;                            // 恐龙受精蛋孵化速度倍率
	uint8                                         SpawnPropRandomIndex;                               // 地图上可生成装饰性道具/植物的随机生成索引种子
	uint8                                         Pad_671[0x3];                                       // 内存对齐填充
	class FName                                   ActiveEvent;                                        // 当前激活的节日/游戏事件名称（如 Easter, Halloween, WinterWonderland）
	bool                                          bAllowPaintingWithoutResources;                     // 是否允许不消耗染料直接进行涂鸦涂色
	bool                                          bVesselSimpleInterpMode;                            // 载具/船只网络同步是否采用简易平滑插值模式
	bool                                          bEnableExtraStructurePreventionVolumes;             // 是否启用额外的禁止建造区域（防穿模/防卡BUG区）
	uint8                                         Pad_67F[0x1];                                       // 内存对齐填充
	float                                         ShipNetworkAndStasisRnageMultiplier;                // 船只网络同步与进入休眠（Stasis）范围的距离倍率
	uint8                                         Pad_684[0x4];                                       // 内存对齐填充
	TArray<struct FItemCraftingCostOverride>      OverrideItemCraftingCosts;                          // 物品制作配方原材料消耗覆盖配置列表
	TArray<struct FItemMaxItemQuantityOverride>   OverrideItemMaxQuantity;                            // 物品最大堆叠/存在数量覆盖配置列表
	bool                                          DisableRailgunPVP;                                  // PVP模式下是否禁用泰克磁轨炮（Tek Railgun）
	uint8                                         Pad_6A9[0xA7];                                      // 内部私有填充（0xA7字节）
	double                                        PrivateNetworkTime;                                 // 内部私有网络高精度时间
	double                                        LastServerSaveTime;                                 // 服务器上一次执行世界自动存档（World Save）的时间戳
	float                                         ServerSaveInterval;                                 // 服务器自动存档的时间间隔（秒）
	float                                         TribeNameChangeCooldown;                            // 修改部落名称的冷却时间
	float                                         PlatformSaddleBuildAreaBoundsMultiplier;            // 平台鞍建造空间范围边界缩放倍率
	bool                                          bAlwaysAllowStructurePickup;                        // 是否允许随时无限制收回拆解放置的建筑
	uint8                                         Pad_76D[0x3];                                       // 内存对齐填充
	float                                         StructurePickupTimeAfterPlacement;                  // 建筑放置后允许快速收回的时间窗口（秒）
	float                                         StructurePickupHoldDuration;                        // 长按按键收回建筑所需要蓄力按住的时间（秒）
	bool                                          bAllowHideDamageSourceFromLogs;                     // 是否允许在部落战日志中隐藏伤害来源/攻击者名称
	uint8                                         Pad_779[0x7];                                       // 内存对齐填充
	TArray<struct FClassMultiplier>               DinoClassSpeedMultipliers;                          // 各类野生恐龙移动速度倍率配置
	TArray<struct FClassMultiplier>               TamedDinoClassSpeedMultipliers;                     // 各类已驯服恐龙移动速度倍率配置
	bool                                          bRiderDinoCollision;                                // 骑手在骑乘状态下是否具有独立碰撞体（防止骑手穿墙）
	bool                                          bNeedsPowerToActivateAquaticCompartments;           // 泰克水下真空仓是否必须通电才能启动防水排气
	uint8                                         Pad_7A2[0x4E];                                      // 内部对齐填充
	class UAudioComponent*                        DynamicMusicAudioComponent;                         // 动态战斗/探索背景音乐音频组件1
	class UAudioComponent*                        DynamicMusicAudioComponent2;                        // 动态背景音乐音频组件2（用于平滑交叉淡入淡出）
	uint8                                         Pad_800[0x18];                                      // 内存对齐填充
	TArray<struct FLevelExperienceRamp>           LevelExperienceRampOverrides;                       // 玩家与恐龙升级经验阈值梯度覆盖表
	TArray<struct FEngramEntryOverride>           OverrideEngramEntries;                              // 印痕技能学习列表覆盖配置（如禁用某些印痕）
	TSubclassOf<class UPrimalUI>                  CustomGameUITemplate;                               // 自定义游戏UI界面模板类
	TArray<class FString>                         PreventDinoTameClassNames;                          // 禁止驯服的恐龙类名黑名单列表
	bool                                          bDisableDefaultDinoTaming;                          // 是否全局禁用所有默认恐龙的驯化功能
	uint8                                         Pad_851[0x7];                                       // 内存对齐填充
	TArray<class FString>                         PreventDisableDefaultDinoTameClassNames;            // 全局禁用驯化时的恐龙白名单列表
	float                                         ListenServerTetherDistanceMultiplier;               // 非专用主机模式下玩家与房主之间的最大系绳（拉扯）距离倍率
	uint8                                         Pad_86C[0x4];                                       // 内存对齐填充
	class FString                                 PGMapName;                                          // 程序化生成地图（Procedural Generated Map）的名称
	TArray<int32>                                 SupportedSpawnRegions;                              // 当前地图支持启用的出生/复活区域ID列表
	class UPaintingCache*                         PaintingCache;                                      // 玩家涂鸦/画板绘制数据的本地缓存对象
	class USoundBase*                             StaticOverrideMusic;                                // 全局静态覆盖播放的背景音乐音效
	bool                                          bEnableDeathTeamSpectator;                          // 玩家阵亡后是否允许以旁观者视角观察本部落队友
	uint8                                         Pad_8A1[0x7];                                       // 内存对齐填充
	struct FVector                                PlayerFloatingHUDOffset;                            // 玩家头顶悬浮信息HUD的三维空间偏移量
	float                                         PlayerFloatingHUDOffsetScreenY;                     // 玩家头顶悬浮信息HUD在屏幕Y轴上的偏移修正
	float                                         StructureDamageRepairCooldown;                      // 建筑受到伤害后进入禁止修理状态的冷却时间（秒）
	bool                                          bForceAllStructureLocking;                          // 是否强制全服所有储物箱/工作台默认上锁
	bool                                          bAllowCustomRecipes;                                // 是否允许玩家制作自定义烹饪配方食品
	bool                                          bAllowRaidDinoFeeding;                              // 是否允许喂养突袭攻城生物（如泰坦龙强制进食）
	uint8                                         Pad_8CB[0x1];                                       // 内存对齐填充
	float                                         CustomRecipeEffectivenessMultiplier;                // 自定义配方效果倍率
	float                                         CustomRecipeSkillMultiplier;                        // 制造技能对自定义配方属性加成的倍率
	uint8                                         Pad_8D4[0x4];                                       // 内存对齐填充
	class USoundBase*                             OverrideAreaMusic;                                  // 特定区域覆盖播放的环境音效/音乐
	struct FVector                                OverrideAreaMusicPosition;                          // 区域环境音乐的发声源中心坐标
	float                                         OverrideAreaMusicRange;                             // 区域环境音乐的可听见半径范围
	bool                                          bAllowUnclaimDinos;                                 // 是否允许玩家弃养/解绑（Unclaim）已驯服的恐龙
	uint8                                         Pad_8FD[0x3];                                       // 内存对齐填充
	float                                         FloatingHUDRange;                                   // 玩家及恐龙名字/等级头顶悬浮HUD的最大可视距离
	float                                         FloatingChatRange;                                  // 头顶悬浮聊天气泡的最大可视距离
	int32                                         ExtinctionEventTimeInterval;                        // 灭绝服/赛季重置事件的周期倒计时总时间（秒）
	float                                         RTSModeNumSelectableDinosScale;                     // RTS指挥模式下允许同时框选恐龙数量的倍率
	float                                         RTSMaxRangeFromPlayerCharacterScale;                // RTS指挥模式下指挥视角距离玩家角色的最大拉高距离倍率
	float                                         ExtinctionEventPercent;                             // 灭绝倒计时已完成的百分比（0.0 ~ 1.0）
	int32                                         ExtinctionEventSecondsRemaining;                    // 灭绝服重置清档剩余倒计时秒数
	bool                                          bDoExtinctionEvent;                                 // 当前是否正在激活灭绝服清档事件
	uint8                                         Pad_91D[0x3];                                       // 内存对齐填充
	int32                                         DestroyTamesOverLevelClamp;                         // 强制销毁超出指定最大等级上限的驯服生物等级阈值（如销毁450级以上恐龙）
	uint8                                         Pad_924[0x4];                                       // 内存对齐填充
	TArray<struct FInventoryComponentDefaultItemsAppend> InventoryComponentAppends;                  // 角色/生物/容器生成时默认追加附带的物品清单
	bool                                          bPreventOfflinePvP;                                 // 是否开启离线防抄家保护（ORP, Offline Raid Protection）
	bool                                          bPvPDinoDecay;                                      // PVP模式下长期不上线的部落恐龙是否自动腐化解绑
	bool                                          bAllowUnclaimDinosConfig;                           // 配置文件中是否开启了解绑弃养恐龙功能
	bool                                          bForceUseInventoryAppends;                          // 是否强制为所有背包注入默认初始物品
	bool                                          bOverideStructurePlatformPrevention;                // 是否覆盖/忽略平台鞍建造防御建筑的限制
	uint8                                         Pad_93D[0x3];                                       // 内存对齐填充
	float                                         ItemStackSizeMultiplier;                            // 全服所有物品堆叠上限倍率（如100变1000）
	uint8                                         Pad_944[0x4];                                       // 内存对齐填充
	TArray<int32>                                 PreventOfflinePvPLiveTeams;                         // 当前处于离线保护状态生效中的部落TeamID列表
	TArray<int32>                                 PreventOfflinePvPExpiringTeams;                     // 离线保护即将过期的部落TeamID列表（倒计时中）
	TArray<float>                                 PreventOfflinePvPExpiringTimes;                     // 离线保护过期部落对应的剩余倒计时时间列表（秒）
	uint8                                         Pad_978[0xA0];                                      // 内部对齐填充（0xA0字节）
	bool                                          bAllowAnyoneBabyImprintCuddle;                      // 是否允许部落内任何成员接替完成幼崽留痕/抱抱任务
	bool                                          bDisableImprintDinoBuff;                            // 是否禁用100%完全留痕后的额外骑乘攻防加成属性
	uint8                                         Pad_A1A[0x2];                                       // 内存对齐填充
	int32                                         MaxPersonalTamedDinos;                              // 单个玩家允许拥有的最大个人驯养生物上限
	uint8                                         Pad_A20[0x18];                                      // 内存对齐填充
	class FString                                 ClusterId;                                          // 当前服务器所在的集群（Cluster）标识ID
	uint8                                         Pad_A48[0x30];                                      // 内存对齐填充
	class FString                                 ServerSessionName;                                  // 服务器在列表里显示的完整房间/服务器名称
	bool                                          bPreventTribeAlliances;                             // 是否禁止部落之间建立联盟关系
	uint8                                         Pad_A89[0x19];                                      // 内存对齐填充
	bool                                          bServerUseDinoList;                                 // 服务器是否启用并维护恐龙全局索引白名单列表
	bool                                          bPvEAllowStructuresAtSupplyDrops;                   // PVE模式下是否允许在空投信标光柱降落点周围建造建筑
	bool                                          bAllowForceNetUpdate;                               // 是否允许客户端请求强制刷新网络同步状态
	uint8                                         Pad_AA5[0x3];                                       // 内存对齐填充
	float                                         MinimumDinoReuploadInterval;                        // 恐龙从云端下载后再次允许上传的最短冷却时间
	float                                         HairGrowthSpeedMultiplier;                          // 玩家角色毛发生长速度倍率
	float                                         DinoHairGrowthSpeedMultiplier;                      // 生物毛发生长速度倍率
	float                                         FastDecayInterval;                                  // 茅草等简易建筑快速解体腐化的时间周期
	class FString                                 CachedSessionOwnerId;                               // 缓存的服务器房主/所有者SteamID或EOS ID
	float                                         OxygenSwimSpeedStatMultiplier;                      // 氧气属性点对水中游泳移动速度的加成倍率
	uint8                                         Pad_ACC[0x4];                                       // 内存对齐填充
	TMulticastInlineDelegate<void(bool bSuccess, const class FString& StringResult)> OnHTTPGetResponse; // HTTP GET异步请求响应到达时的回调多播委托
	TMulticastInlineDelegate<void(bool bSuccess, const class FString& StringResult)> OnHTTPPostResponse; // HTTP POST异步请求响应到达时的回调多播委托
	bool                                          bAllowMultipleAttachedC4;                           // 是否允许在单个目标身上同时吸附多个C4炸药
	bool                                          bCrossARKAllowForeignDinoDownloads;                 // 是否允许从其他非官方/外部不同地图的方舟下载外来异种恐龙
	uint8                                         Pad_AF2[0x16];                                      // 内存对齐填充
	bool                                          bUseCorpseLocator;                                  // 死亡后是否在空中生成指向尸体/背包的绿色光柱指示器
	bool                                          bEnablePlayerMoveThroughSleeping;                   // 玩家是否可以穿过离线睡觉倒地玩家的碰撞体
	bool                                          bEnablePlayerMoveThroughAlly;                       // 玩家是否可以穿透同部落/同盟友军的身体（关闭玩家间碰撞）
	bool                                          bDisableStructurePlacementCollision;                // 是否禁用建筑摆放时的地形穿模碰撞检测（允许随处卡入地里建造）
	bool                                          bUseSingleplayerSettings;                           // 是否启用了单人模式专用数值平衡配置
	bool                                          bAllowPlatformSaddleMultiFloors;                    // 平台鞍是否允许建造多层建筑结构
	bool                                          bPreventSpawnAnimations;                            // 是否全局禁用所有玩家复活时的动画
	uint8                                         Pad_B0F[0x1];                                       // 内存对齐填充
	int32                                         MaxAlliancesPerTribe;                               // 每个部落允许加入的最大联盟数量上限
	int32                                         MaxTribesPerAlliance;                               // 每个联盟内允许容纳的最大部落数量上限
	bool                                          bIsLegacyServer;                                    // 是否为早期官方遗留旧服（Legacy Server）
	bool                                          bDisableDinoDecayClaiming;                          // 恐龙腐化解绑后是否禁止其他玩家认领/占为己有
	uint8                                         Pad_B1A[0x2];                                       // 内存对齐填充
	int32                                         bEnableServerDestroyTamesAboveSoftTameLimit;         // 超出软驯养上限后是否强制开启定时删除超额恐龙机制
	int32                                         MaxTamedDinos_SoftTameLimit_CountdownForDeletionDuration; // 达到软驯服上限后触发删除超额恐龙的倒计时总时长（秒）
	int32                                         OverrideSecondsUntilBuriedTreasureAutoReveals;       // 埋藏的宝藏/宝箱自动浮现显形前的倒计时秒数
	int32                                         MaxTamedDinos_SoftTameLimit_NumTamedDinos_UnderLimit; // 当前处于软上限安全线以内的恐龙数量
	int32                                         MaxTamedDinos_SoftTameLimit_NumTamedDinos_OverLimit;  // 当前超出软上限需要被排队删除的超额恐龙数量
	bool                                          MaxTamedDinos_SoftTameLimit_HasDoneInitialSearch;   // 服务器启动后是否已经完成了软上限超额恐龙的初始扫描
	uint8                                         Pad_B31[0x3];                                       // 内存对齐填充
	class FName                                   UseStructurePreventionVolumeTag;                    // 生效中的禁止建造区域的体积标签名称
	int32                                         MaxStructuresInSmallRadius;                         // 在小半径密集区域内允许放置的最大建筑数量
	float                                         RadiusStructuresInSmallRadius;                      // 用于判定建筑密集度的小半径范围大小
	int32                                         MaxAnchoredVesselsInRange;                          // 区域内允许停靠抛锚的最大船只/载具数量
	int32                                         AnchoredVesselCheckRadius;                          // 检测抛锚船只密集度的搜索半径范围
	bool                                          bUseTameLimitForStructuresOnly;                     // 上限限制是否只针对建筑生效而不针对生物
	bool                                          bDisableWirelessCrafting;                           // 是否全局禁用无线合成/无线制造功能
	bool                                          bDisableWirelessCraftingForDinos;                   // 是否禁用从附近恐龙身上无线抓取材料合成
	bool                                          bDisableWirelessCraftingForPlayers;                 // 是否禁用玩家背包直接无线抓取箱子材料合成
	bool                                          bDisableWirelessCraftingForStructures;              // 是否禁用工作台/铁匠铺无线抓取箱子材料合成
	uint8                                         Pad_B51[0x3];                                       // 内存对齐填充
	float                                         WirelessCraftingRangeOverride;                      // 无线合成抓取材料的最大距离覆盖值
	uint8                                         Pad_B58[0xC];                                       // 内存对齐填充
	bool                                          bLimitTurretsInRange;                               // 是否开启一定区域内自动炮塔数量硬上限限制
	uint8                                         Pad_B65[0x3];                                       // 内存对齐填充
	float                                         LimitTurretsRange;                                  // 炮塔数量限制检测的半径范围（如10000厘米）
	int32                                         LimitTurretsNum;                                    // 限制半径内允许放置的最大激活炮塔数量（如100个）
	bool                                          bLimitBunkersPerTribe;                              // 是否限制每个部落允许建造的地堡/掩体总数
	uint8                                         Pad_B71[0x3];                                       // 内存对齐填充
	int32                                         LimitBunkersPerTribeNum;                            // 每个部落允许建造的最大地堡数量上限
	TArray<uint32>                                BunkersPerTribe;                                    // 记录各部落当前已建造地堡数量的动态映射数组
	float                                         MinDistanceBetweenBunkers;                          // 两个地堡之间必须保持的最小建造安全间距
	bool                                          bAllowBunkersInPreventionZones;                     // 是否允许在禁建区内建造地堡
	bool                                          bAllowBunkerModulesAboveGround;                     // 地堡扩展模块是否允许露出地表建造
	bool                                          bAllowBunkerModulesInPreventionZones;               // 是否允许在禁建区内部署地堡模块
	uint8                                         Pad_B8F[0x1];                                       // 内存对齐填充
	float                                         CryoHospitalHoursToRegenHP;                         // 低温仓/冷冻医院给恐龙回满生命值所需的游戏内小时数
	float                                         CryoHospitalHoursToRegenFood;                       // 低温仓给恐龙回满食物度所需的游戏内小时数
	float                                         CryoHospitalHoursToDrainTorpor;                     // 低温仓给恐龙排空眩晕值所需的游戏内小时数
	float                                         CryoHospitalMatingCooldownReduction;                // 低温仓内恐龙交配冷却时间的缩减速度倍率
	float                                         BloodforgeReinforceExtraDurability;                 // 鲜血熔炉强化提供的额外装备/护甲耐久度数值
	float                                         BloodforgeReinforceResourceCostMultiplier;          // 鲜血熔炉强化消耗资源的倍率
	float                                         BloodforgeReinforceSpeedMultiplier;                 // 鲜血熔炉强化工艺的执行速度倍率
	int32                                         LimitGeneratorsNum;                                 // 限制范围内允许放置的发电机最大数量
	float                                         LimitGeneratorsRange;                               // 发电机数量限制的检测范围半径
	float                                         TribeTowerBonusMultiplier;                          // 部落防卫塔/领地图腾提供的增益属性加成倍率
	int32                                         CropPlotStackLimit;                                 // 耕地/农田允许垂直重叠堆叠放置的数量上限
	bool                                          bDisablePhotoMode;                                  // 是否禁用游戏内的拍照/摄影模式（防止利用摄影穿墙透视探点）
	bool                                          bDisableTekLegsBoost;                               // 是否禁用泰克裤子的高速冲刺过载加速功能
	uint8                                         Pad_BBE[0x2];                                       // 内存对齐填充
	int32                                         MaxCosmoWeaponAmmo;                                 // 宇宙蜘蛛（Cosmo）武器形态下的最大弹药容量
	float                                         ArmadoggoDeathCooldown;                             // 铠犬（Armadoggo）死亡后再次召唤/复活的冷却时间
	float                                         YoungIceFoxDeathCooldown;                           // 幼年冰狐死亡冷却时间
	float                                         CompanionsDeathCooldown;                            // 伙伴生物类死亡冷却时间基准
	int32                                         CosmoWeaponAmmoReloadAmount;                        // 宇宙蜘蛛武器每次重装填补充的弹药数量
	float                                         PhotoModeRangeLimit;                                // 拍照模式下自由漫游摄像机允许拉远的最大距离
	class FString                                 ValgueroMemorialEntries;                            // 瓦尔盖罗（Valguero）地图纪念碑刻字内容文本
	bool                                          bForceAllowAllStructures;                           // 是否强制允许摆放所有类型的建筑（无视任何环境限制）
	bool                                          bShowCreativeMode;                                  // 暂停菜单中是否向玩家显示“开启创造模式”按钮
	uint8                                         Pad_BEA[0x26];                                      // 内存对齐填充
	float                                         PassiveTameIntervalMultiplier;                      // 和平驯服（被动喂食）时间间隔倍率（越小喂食越频繁）
	uint8                                         Pad_C14[0x4];                                       // 内存对齐填充
	TArray<TSubclassOf<class APrimalDinoCharacter>> UniqueDinos;                                      // 全服唯一/传奇生物（如全服限量泰坦）的类引用列表
	TArray<class FName>                           ActiveMissionTags;                                  // 创世纪等地图当前正在激活执行中的任务Tag标签
	uint32                                        MinimumUniqueDownloadInterval;                      // 下载唯一生物的最短冷却间隔（秒）
	uint32                                        MaximumUniqueDownloadInterval;                      // 下载唯一生物的最大冷却间隔（秒）
	bool                                          bIgnoreStructuresPreventionVolumes;                 // 是否全局忽略所有禁止建造区域检测
	bool                                          bPreventOutOfTribePinCodeUse;                       // 是否禁止非部落成员输入密码（PIN Code）打开箱子/防盗门
	uint8                                         Pad_C42[0x6];                                       // 内存对齐填充
	TArray<struct FDinoDownloadData>              UniqueDownloads;                                    // 唯一生物下载排队与历史数据列表
	TArray<struct FDataSet>                       GameDataSets;                                       // 游戏全局动态数据集配置
	class UPrimalWorldSettingsEventOverrides*     ActiveEventOverrides;                               // 当前激活节日事件的世界配置重载对象指针
	bool                                          bIgnoreLimitMaxStructuresInRangeTypeFlag;           // 是否忽略特定类型建筑在区域范围内的最大数量限制
	uint8                                         Pad_C71[0x7];                                       // 内存对齐填充
	TArray<struct FMassTeleportData>              MassTeleportQueue;                                  // 正在执行的群体传送（如泰克传送门、任务传送）任务队列
	uint8                                         Pad_C88[0x70];                                      // 内部对齐填充（0x70字节）
	TMap<class FName, class UDataLayerInstance*>  DataLayerMap;                                       // UE5世界分区数据层（Data Layers）映射表
	bool                                          bAllowLowGravitySpin;                               // 低重力（太空/月球）环境下是否允许自由翻滚旋转
	uint8                                         Pad_D49[0x17];                                      // 内存对齐填充
	TMulticastInlineDelegate<void(class APrimalDinoCharacter* TheDino, class AShooterPlayerController* TamerController)> OnDinoDownloaded; // 恐龙从方舟云端下载落地时的多播事件
	TMulticastInlineDelegate<void(class APrimalDinoCharacter* TheDino)> OnDinoUploaded;                // 恐龙被上传到方舟云端时的多播事件
	TArray<struct FWorldBuffPersistantData>       WorldBuffPersistantDatas;                           // 全服持久化增益Buff数据列表（如全服双倍经验、全服环境Debuff）
	float                                         TurretCopySettingsCooldown;                         // 炮塔之间复制粘贴配置参数的冷却时间
	float                                         BaseHexagonRewardMultiplier;                        // 创世纪任务通关获得海克斯六角形点数的基准奖励倍率
	float                                         HexagonRewardMultiplier;                            // 海克斯点数获取的额外全局倍率
	float                                         HexagonCostMultiplier;                              // 海克斯商店购买物品消耗点数的倍率
	bool                                          bDisableHexagonStore;                               // 是否彻底关闭海克斯商店（HLN-A商店）
	bool                                          bHexStoreAllowOnlyEngramTradeOption;                // 海克斯商店是否仅开放印痕解锁交易
	bool                                          bAllowSpeedLeveling;                                // 玩家升级时是否允许加点“移动速度”属性
	bool                                          bAllowFlyerSpeedLeveling;                           // 飞行恐龙升级时是否允许加点“飞行速度”属性
	uint8                                         Pad_DA4[0x4];                                       // 内存对齐填充
	TArray<class FString>                         PreventBreedingForClassNames;                       // 禁止交配/繁衍的恐龙类名黑名单列表
	int32                                         LiveTuningReplicatedChunkSize;                      // 动态热更新/在线调优数据同步的分块大小
	uint8                                         Pad_DBC[0x4];                                       // 内存对齐填充
	TArray<class FString>                         LiveTuningOverloadChunks;                           // 动态热更新的重载数据块列表
	uint8                                         Pad_DD0[0x8];                                       // 内存对齐填充
	TArray<class FString>                         PreventTransferForClassNames;                       // 禁止跨服传送/跨服转移的物品/恐龙类名列表
	int32                                         EnvironmentIndex;                                   // 创世纪2/特定地图当前环境状态索引（如太空飞船变换区域资源类型）
	int32                                         NextEnvironmentIndex;                               // 下一次即将切换的环境状态索引
	struct FColor                                 FloatingPlatformProfileNameColor;                   // 浮空平台鞍/船只头顶显示的名称文字颜色
	struct FColor                                 FloatingNameColor;                                  // 普通实体头顶浮动名称默认渲染文字颜色
	double                                        NextMutagenTime;                                    // 下一次地图刷新诱变剂（Mutagen）的世界时间戳
	int32                                         OverrideMaxExperiencePointsPlayer;                  // 玩家允许获取的最高总经验上限覆盖值
	int32                                         OverrideMaxExperiencePointsDino;                    // 恐龙允许获取的最高总经验上限覆盖值
	TArray<int32>                                 DeferredExplorerNoteUnlockQueue;                    // 延迟解锁的探险者笔记/图鉴ID排队队列
	bool                                          bDisableCustomFoldersInTributeInventories;          // 是否禁止在方舟终端背包内创建自定义文件夹
	uint8                                         Pad_E19[0x7];                                       // 内存对齐填充
	TArray<TWeakObjectPtr<class AActor>>          ForcedRelevantPOIActors;                            // 强制保持网络相关性（不进休眠、全图同步）的重点兴趣点Actor列表
	bool                                          bDisableRailgunPVP;                                 // PVP中是否禁用泰克磁轨炮（重复定义的标志位）
	uint8                                         Pad_E31[0x3];                                       // 内存对齐填充
	float                                         MinimumTimebetweeninventoryRetrieval;               // 从尸体背包/远程容器抓取物品的最小间隔时间
	float                                         WildFollowerSpawnChanceMultiplier;                  // 野生跟随生物生成概率倍率
	float                                         WildFollowerSpawnCountMultiplier;                   // 野生跟随生物生成数量倍率
	float                                         ServerGeneTraitSpawnRateMultiplier;                 // ASA新机制：基因特质（Gene Traits）在野生龙身上的生成概率倍率
	bool                                          bDisableDinoItemBlacklist;                          // 是否禁用恐龙背包物品装载黑名单（如允许恐龙背某些特殊重型资源）
	bool                                          bAllowCryoFridgeOnSaddle;                           // 是否允许在平台鞍上放置低温冷藏箱（低温冰箱）
	bool                                          bPreventTemplateOnSaddle;                           // 是否禁止在平台鞍上使用预设建筑模板
	bool                                          bIgnorePVPMountedWeaponryRestrictions;              // 是否忽略PVP中骑乘状态下的开火限制（允许在所有龙身上持枪射击）
	uint8                                         Pad_E48[0x50];                                      // 内部对齐填充（0x50字节）
	double                                        RealtimeThrottledTickTimeAmount;                    // 动态节流Tick消耗的实时时间总量
	uint8                                         Pad_EA0[0x4];                                       // 内存对齐填充
	uint8                                         bCharacterPaintingOnlySameTeam : 1;                 // 位域：是否仅允许同部落友军给玩家角色身体画画
	bool                                          bAllowTeslaCoilCaveBuildingPVP;                     // PVP模式下是否允许在洞穴中建造特斯拉电塔
	bool                                          bDisableGeneTraits;                                 // ASA机制：是否全局禁用恐龙基因特质系统
	bool                                          bForceGachaUnhappyInCaves;                          // 是否强制使洞穴内的嘎查（Gacha）始终处于不开心/郁闷状态
	bool                                          bDontEnforceMilestoneTaskOrder;                     // 是否允许不按顺序自由完成里程碑/成就任务
	uint8                                         Pad_EA9[0x7];                                       // 内存对齐填充
	TArray<struct FNetChangeDefaultPropertyValue> CurrentDefaultPropertyValueUpdates;                 // 当前网络同步修改的默认属性值更新列表
	uint8                                         Pad_EC0[0x18];                                      // 内存对齐填充
	TArray<struct FCustomCosmeticModData>         WhiteListedCustomCosmeticModSkins;                  // 官方/服务器白名单允许加载的自定义外观Mod皮肤列表
	bool                                          bDoCustomCosmeticValidation;                        // 是否对玩家加载的自定义皮肤进行安全性校验
	bool                                          bDoAutomatedModValidationModeration;                // 是否开启自动化的Mod合规性审查机制
	uint8                                         Pad_EEA[0x6];                                       // 内存对齐填充
	TArray<int64>                                 NetBanlistedMods;                                   // 服务器拉黑禁用的Mod ID列表
	uint8                                         Pad_F00[0x90];                                      // 内部对齐填充（0x90字节）
	TMap<int32, int32>                            ThrallTargetingTeamCount;                           // 奴隶/仆从/自动守卫目标部落当前索敌追踪计数映射表
	uint8                                         Pad_FE0[0x8];                                       // 尾部对齐填充

public:
	// --- 静态成员函数 ---

	// 在 Canvas 上绘制带纹理贴图的矩形平铺图像（用于绘制准星、自定义图标、小地图等）
	static void BaseDrawTileOnCanvas(class AShooterHUD* HUD, class UTexture* Tex, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, const struct FColor& DrawColor);

	// 判断给定的 TeamID 是否属于合法部落（ARK 中 TeamID >= 2000000000 通常代表部落ID，小于则是个人/NPC ID）
	static bool BaseIsTribeID(int32 TeamID);

	// 将世界三维坐标投影为玩家屏幕二维坐标（W2S 世界坐标转屏幕坐标）
	static struct FVector2D BaseProjectWorldToScreenPosition(const struct FVector& WorldLocation, class APlayerController* ThePC);

	// 为指定角色生成一个增益/减益 Buff 并挂载上去，同时赋予一定经验值
	static class APrimalBuff* BaseSpawnBuffAndAttachToCharacter(class UClass* Buff, class APrimalCharacter* PrimalCharacter, float ExperiencePoints);

	// 从类默认对象（CDO）中读取指定属性列表并输出为可读字符串（用于反射与调试输出）
	static bool CDOGetPrintStringForObjectProperties(class UObject* TheObject, TArray<class FName>& PropertyNames, class FString* OutString);

	// 从类默认对象（CDO）中读取单个属性并输出为可读字符串
	static bool CDOGetPrintStringForObjectProperty(class UObject* TheObject, class FName PropertyName, class FString* OutString);

	// 计算当前服务器网络时间与目标网络时间之间的时间差（Delta）（可选计算直到该时间过去的剩余时间）
	static double GetNetworkTimeDelta(class AShooterGameState* GameState, double netTime, bool bTimeUntil);

	// 检查群体传送结构体数据是否有效合法
	static bool IsValidMassTeleportData(const struct FMassTeleportData& CheckData);

	// 打印角色类默认对象（CDO）的调试数据到日志
	static void PrintCDODebug(class APrimalCharacter* ReferenceChar, struct FCDODebugData& CDODebugData);


	// --- 实例成员函数 ---

	// 在指定世界坐标生成一段带物理弹道运动、淡入淡出、指定寿命的浮动文字（用于伤害数字、状态提示）
	void AddFloatingText(const struct FVector& AtLocation, const class FString& FloatingTextString, const struct FColor& FloatingTextColor, float ScaleX, float ScaleY, float TextLifeSpan, const struct FVector& TextVelocity, float MinScale, float FadeInTime, float FadeOutTime);

	// 动态添加指定物品类的最大堆叠数量覆盖配置
	void AddMaxItemQuantityOverride(TSubclassOf<class UPrimalItem> ItemClass, const struct FMaxItemQuantityOverride& Override);

	// 添加一个强制全图保持网络同步的相关性重点兴趣点Actor（如补给空投箱、世界Boss）
	void AddRelevantPOIActor(class AActor* POI);

	// 为指定部落增加代币/积分点数（常用于海克斯点数或Mod交易系统）
	void AddTokens(int32 Quantity, int32 byTribe);

	// 查询指定玩家控制器是否被允许驯服某种类型的恐龙（根据黑白名单判断）
	bool AllowDinoClassTame(TSubclassOf<class APrimalDinoCharacter> DinoCharClass, class AShooterPlayerController* ForPC);

	// 查询指定玩家控制器是否允许驯服特定这只恐龙实例
	bool AllowDinoTame(class APrimalDinoCharacter* DinoChar, class AShooterPlayerController* ForPC);

	// 检查某个恐龙类是否允许从方舟云端下载到本服
	bool AllowDownloadDino(const TSoftClassPtr<class UClass>& TheDinoClass);

	// 获取全图属于指定队伍/部落（TeamID）的所有恐龙生物实例列表
	TArray<class APrimalDinoCharacter*> BaseGetAllDinoCharactersOfTeam(int32 KillerTeam);

	// 获取全图当前已生成的所有玩家角色实例列表（包括离线挂机玩家）
	TArray<class AShooterCharacter*> BaseGetAllShooterCharacters();

	// 获取全图属于指定队伍/部落的所有玩家角色实例列表
	TArray<class AShooterCharacter*> BaseGetAllShooterCharactersOfTeam(int32 KillerTeam);

	// 获取全服当前在线的所有玩家控制器（PlayerController）列表
	TArray<class AShooterPlayerController*> BaseGetAllShooterControllers();

	// 根据自定义Tag标签查找世界中的自定义Actor列表管理器
	class ACustomActorList* BaseGetCustomActors(class FName SearchCustomTag);

	// 蓝图绘制 GameState 专属的 HUD 元素（底层原生绘制钩子）
	void BPDrawGameStateHUD(class AShooterHUD* HUD);

	// 网络同步：在指定世界坐标生成一个Actor并同步给附近的客户端，可附带网络距离和插槽绑定
	void BPNetSpawnActorAtLocation(TSubclassOf<class AActor> anActorClass, const struct FVector& AtLocation, const struct FRotator& AtRotation, class AActor* EffectOwnerToIgnore, float MaxRangeToReplicate, class USceneComponent* AttachToComponent, int32 DataIndex, class FName AttachSocketName, bool bOnlySendToEffectOwner, class APawn* UseInstigator);

	// 游戏开始前执行的底层准备与预设置逻辑
	void BPPreGameplaySetup();

	// 取消由指定发起者触发的群体传送进程
	bool CancelMassTeleport(const class AActor* WithInitiatingActor);

	// 减少指定部落被守卫/奴隶锁定的目标计数
	void DecrementThrallTargetingTeamCount(int32 TeamID);

	// 弹出并显示服务器欢迎界面/今日消息（MOTD）
	void DisplayWelcomeUI();

	// 强制开始对局比赛（常用于大逃杀/SOTF模式，支持使用风神大巴进场）
	void ForceStartMatch(bool PreventFinishTheMatch, bool UseQuetzalBus);

	// 获取指定部落当前在全服已建造的地堡建筑总数量
	int32 GetBunkerAmountForTeam(int32 ForTeam);

	// 获取游戏内当前时间的可读格式化字符串（例如返回 "12:30", "Day 42"）
	class FString GetDayTimeString();

	// 解析并获取 Game.ini / GameUserSettings.ini 中指定段落配置的结构体数组
	TArray<struct FGameIniData> GetIniArray(const class FString& SectionName);

	// 获取指定物品类的最大堆叠数量覆盖配置结构体
	bool GetItemMaxQuantityOverride(TSubclassOf<class UPrimalItem> ForClass, struct FMaxItemQuantityOverride* OutMaxQuantity);

	// 获取当前被守卫/仆从锁定攻击最少的队伍ID（用于仇恨均衡）
	int32 GetLeastPopulatedThrallTargetingTeam();

	// 获取指定队伍剩余的离线防抄家保护（ORP）生效倒计时时间
	float GetOfflineDamagePreventionTime(int32 TargetingTeamID);

	// 处理玩家进出洞穴时的环境雾气能见度切换（洞穴内外视野平滑过渡）
	void HandleFogVisibiltyInCaves(bool bInCave);

	// GameState 初始化就绪后的内部处理钩子
	void HandleInitializedGameState();

	// 向指定远程 Web URL 发起异步 HTTP GET 请求
	void HTTPGetRequest(const class FString& InURL);

	// 向指定远程 Web URL 发起异步 HTTP POST 请求并携带内容
	void HTTPPostRequest(const class FString& InURL, const class FString& Content);

	// 增加指定部落被守卫锁定的目标计数
	void IncreementThrallTargetingTeamCount(int32 TeamID);

	// 检查指定物品的印痕配方是否在科技树中被隐藏/禁用
	bool IsEngramClassHidden(TSubclassOf<class UPrimalItem> ForItemClass);

	// 检查指定 Actor 发起的群体传送是否正在执行中
	bool IsMassTeleportInProgress(class AActor* InitiatingActor);

	// 检查指定队伍当前是否处于无敌保护状态（支持只在离线时判定无敌）
	bool IsTeamIDInvincible(int32 TargetingTeamID, bool bInvincibleOnlyWhenOffline);

	// 检查指定队伍的地堡数量是否尚未超出部落上限
	bool IsUnderBunkerTribeLimit(int32 ForTeam);

	// 检查某种全服唯一生物（如Boss、泰坦）是否已经在世界中被生成出来
	bool IsUniqueDinoAlreadySpawned(const TSoftClassPtr<class UClass>& UniqueDino);

	// 世界地图关卡完全加载完成时的内部通知函数
	void LoadedWorld();

	// 多播RPC：在所有客户端指定位置生成一个纯视觉表现的装饰性Actor（如粒子光效）
	void Multi_SpawnCosmeticActor(TSubclassOf<class AActor> SpawnActorOfClass, const struct FVector& SpawnAtLocation, const struct FRotator& SpawnWithRotation);

	// 网络同步：在指定位置生成飘字伤害数值，并定向同步给对应队伍
	void NetAddFloatingDamageText(const struct FVector& AtLocation, int32 DamageAmount, int32 FromTeamID, int32 OnlySendToTeamID);

	// 网络同步：在指定位置生成通用悬浮文字，可定向同步给指定队伍或执行特定任务的玩家
	void NetAddFloatingText(const struct FVector& AtLocation, const class FString& FloatingTextString, const struct FColor& FloatingTextColor, float ScaleX, float ScaleY, float TextLifeSpan, const struct FVector& TextVelocity, float MinScale, float FadeInTime, float FadeOutTime, int32 OnlySendToTeamID, class AMissionType* OnlySendToCharsOnMission);

	// 网络同步：在指定坐标生成植被砍伐/破坏的粒子特效并触发附近树木/灌木丛物理倒伏交互
	void NetSpawnFoliageVFXActorAtLocationAndDoFoliageInteraction(TSubclassOf<class AActor> anActorClass, const struct FVector_NetQuantize& AtLocation, const struct FRotator_NetQuantize& AtRotation, class AActor* EffectOwnerToIgnore, float MaxRangeToReplicate, class USceneComponent* AttachToComponent, int32 DataIndex, class FName AttachSocketName, bool bOnlySendToEffectOwner, bool IsSimpleFoliageInteraction, const struct FVector& FoliageOrigin_ImpactPoint, const struct FVector& TraceEndpoint, class APawn* UseInstigator);

	// 网络同步：更新离线保护即将过期的部落列表及其剩余时间
	void NetUpdateOfflinePvPExpiringTeams(const TArray<int32>& NewPreventOfflinePvPExpiringTeams, const TArray<float>& NewPreventOfflinePvPExpiringTimes);

	// 网络同步：更新当前离线保护已完全激活生效的部落列表
	void NetUpdateOfflinePvPLiveTeams(const TArray<int32>& NewPreventOfflinePvPLiveTeams);

	// 接收并处理 HUD 屏幕通知横幅弹窗
	bool OnHUDNotification(const struct FHUDNotification& notification);

	// 当引擎初始化了一个新的 UClass 类时的反射注册通知
	void OnNewClassInitialized(class UClass* ForClass);

	// 网络属性复制响应：自定义外观皮肤白名单更新
	void OnRep_CosmeticWhitelist();

	// 网络属性复制响应：默认属性值重载更新
	void OnRep_CurrentDefaultPropertyValueUpdates();

	// 网络属性复制响应：自动化 Mod 合规审查状态更新
	void OnRep_DoAutomatedModValidationModeration();

	// 网络属性复制响应：Mod 封禁黑名单更新
	void OnRep_NetBanlistedMods();

	// 网络属性复制响应：在线热调优配置块同步更新
	void OnRep_ReplicateLiveTuningOverloadChunks();

	// 网络属性复制响应：支持的复活出生区域列表更新
	void OnRep_SupportedSpawnRegions();

	// 为群体传送准备目标 Actor（冻结动作、挂载传送前特效）
	void PrepareActorForMassTeleport(class AActor* PrepareActor, const struct FMassTeleportData& WithMassTeleportData);

	// 打印指定角色的类默认对象（CDO）调试信息
	void PrintCDODebugForChar(class APrimalCharacter* ReferenceChar);

	// 将一个探险者笔记/图鉴压入延迟解锁排队队列
	void QueueExplorerNoteForDeferredUnlock(int32 ExplorerNoteIndex);

	// 移除指定物品类的最大堆叠数量覆盖配置
	void RemoveMaxItemQuantityOverride(TSubclassOf<class UPrimalItem> ItemClass);

	// 移除一个强制保持全图网络同步的兴趣点 Actor
	void RemoveRelevantPOIActor(class AActor* POI);

	// 服务端更新地图随机装饰物生成种子索引
	void ServerUpdateSpawnPropRandomIndex();

	// 设置当前地图环境状态索引（如创世纪2太空飞船资源区类型）
	void SetEnvironmentIndex(int32 newEnvironmentIndex);

	// 预设下一个即将切换的地图环境状态索引
	void SetNextEnvironmentIndex(int32 newNextEnvironmentIndex);

	// 启动一次群体传送任务（传入目标位置、传送半径、参与实体列表及附加Buff）
	bool StartMassTeleport(struct FMassTeleportData* MassTeleportData, const struct FTeleportDestination& TeleportDestination, class AActor* InitiatingActor, const TArray<class AActor*>& TeleportActors, TSubclassOf<class APrimalBuff> BuffToApply, const float TeleportDuration, const float TeleportRadius, const bool bTeleportingSnapsToGround, const bool bMaintainRotation, const bool bTeleportingSnapsToStationaryObjects);

	// 根据加密哈希后的唯一 ID 查找对应的玩家状态对象（PlayerState）
	class APlayerState* FindPlayerStateFromHashedUniqueID(uint32 HashedUniqueID) const;

	// 获取过滤清洗掉颜色代码与特殊符号后的纯净服务器房间名字符串
	class FString GetCleanServerSessionName() const;

	// 获取当前昼夜循环时间流速的绝对倍率
	float GetDayCycleSpeed() const;

	// 获取当前游戏所处的阶段状态（如准备中、进行中、已结束等枚举）
	EGamePhase GetGamePhase() const;

	// 获取游戏初始化时的起始小时整型值
	int32 GetStartTimeHour() const;

	// 判定在群体传送执行时，是否应当将指定 Actor 移动到位移终点
	bool ShouldMassTeleportMoveActor(class AActor* ForActor, const struct FMassTeleportData& WithMassTeleportData) const;
};