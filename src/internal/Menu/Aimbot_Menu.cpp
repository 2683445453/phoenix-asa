#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../Config/Configs.h"
#include "ConfigImGui.h"
#include "Aimbot_Menu.h"
#include "../Legit/Aimbot/Aimbot.h"
#include "../Hack/TekSpear.h"
#include "../Hack/Hack.h"
#include "../Hack/TekSpearRules.h"
#include "../../external/CppSDK/SDK.hpp"
#include "../Language/LanguageManager.h"

namespace g_DrawImGui {
    void Aimbot_Menu() {
        const char* tabLabel = LanguageManager::Aimbot_Menu::TabLabel;
        const char* secTitle = LanguageManager::Aimbot_Menu::SectionTitle;
        const char* trigEnabled = LanguageManager::Aimbot_Menu::TriggerbotEnabled;
        const char* trigHitChance = LanguageManager::Aimbot_Menu::TriggerHitChance;

        if (!BeginMenuPage(MenuPage::Combat, tabLabel)) return;
        BeginTabRegion("CombatDetail");
        ImGui::BeginDisabled(!g_Hook::UpdateCameraOK && !g_Hook::PostRenderOK);
        if (IsTopic(Topic::Aim)) {
                DrawCustomCheckbox(Tr(U8("自动锁定（魔术子弹）")), &g_Config::bAimbotEnabled, U8("自动选择锁定圆内的敌方目标并修正武器射击方向，镜头仍由你控制。先选目标类型与头部／身体，再设置距离和锁定圆。遮挡或超出武器射程时暂停修正。"));
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_1", g_Config::bAimbotEnabled); Fade) {
                HelpText(Tr(U8("锁定圆内头部／身体到圆心连线最短的目标，镜头由你控制。")));
                ImGui::TextWrapped(U8("%s"), g_Aimbot::GetAimbotStatus().c_str());
                DrawChoiceGroup(U8("目标类型"), {
                    {Tr(U8("瞄准敌对玩家")), false, nullptr, &g_Config::bAimPlayers},
                    {Tr(U8("瞄准野生龙")), false, nullptr, &g_Config::bAimWildDinos},
                    {Tr(U8("瞄准敌对驯养龙")), false, nullptr, &g_Config::bAimTamedDinos},
                    {Tr(U8("瞄准 Boss")), false, nullptr, &g_Config::bAimBosses}
                });
                HelpText(Tr(U8("同部落的玩家、驯养生物始终视为友方，不会被自瞄锁定；野生龙按开关决定，Boss 单独筛选。")));
                DrawChoiceGroup(U8("锁定部位"), {
                    {Tr(U8("锁定头部")), false, nullptr, &g_Config::bAimHead},
                    {Tr(U8("锁定身体")), false, nullptr, &g_Config::bAimBody}
                });
                HelpText(Tr(U8("两项都开时按离圆心最近的部位选择；只开头部不回退身体。")));
                if (!g_Config::bAimHead && !g_Config::bAimBody)
                    ImGui::TextWrapped(Tr(U8("头部与身体均已关闭，暂停锁定与自动开火。")));
                DrawCustomCheckbox(Tr(U8("锁定时消除枪械散布")), &g_Config::bAimRemoveSpread);
                HelpText(Tr(U8("有效锁定时取消即时枪械的随机偏移，默认开启；不改变射程。")));
                if (g_Config::bAimRemoveSpread && !g_Hook::ShotgunSpreadOK)
                    ImGui::TextWrapped(Tr(U8("当前客户端未匹配散布接口，锁定后仍可能因散布打偏。")));
                DrawCustomCheckbox(Tr(U8("投射物自动预判")), &g_Config::bProjectilePrediction);
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_2", g_Config::bProjectilePrediction); Fade) {
                    DrawCustomSliderFloat(Tr(U8("最大预判飞行时间")), &g_Config::ProjectilePredictionMaxSeconds, 0.1f, 10.0f, "%.1f", 0.1f, "s");
                    DrawCustomSliderFloat(Tr(U8("额外延迟补偿")), &g_Config::ProjectilePredictionDelayMs, 0.0f, 300.0f, "%.0f", 10.0f, "ms");
                    HelpText(Tr(U8("使用当前弹速、重力、蓄力和目标速度，支持泰克步枪、流星锤、复合弓、弩，以及泰克弓实体射箭模式的金属、爆炸、麻醉和火焰箭。爆炸箭单独计算蓄力弹速；即时命中模式无需飞行预判。")));
                }
                DrawCustomSliderFloat(Tr(U8("瞄准最大距离")), &g_Config::AimbotMaxDistanceMeters,
                    10.0f, PhoenixInterface::MaxAimDistance, "%.0f", 10.0f, "m");
                HelpText(Tr(U8("选目标只按圆心距离；被遮挡或超出武器射程会暂停修正，保留选中对象。")));
                ImGui::TextWrapped(U8("%s"), g_Aimbot::GetMagicBulletStatus().c_str());
                HelpText(Tr(U8("检查从原生发射起点出发的实际弹道遮挡。")));
                DrawCustomSliderFloat(Tr(U8("锁定圆大小")), &g_Config::AimbotCircleRadiusPercent,
                    1.0f, 100.0f, "%.0f", 1.0f, "%");
                HelpText(Tr(U8("按屏幕短边缩放；关闭圆或指示器显示仍按此范围锁定。")));
                DrawAnimatedSeparator();

                }

        }
        if (IsTopic(Topic::Weapons)) {
            Title(Tr(U8("操控与瞄准")));
            DrawCustomCheckbox(Tr(U8("快速开镜")), &g_Config::bFastAim);
            DrawCustomCheckbox(Tr(U8("消除开火后座抖动与瞄准漂移")), &g_Config::bNoRecoil);
            DrawCustomCheckbox(Tr(U8("疾跑时开枪与近战挥砍")), &g_Config::bSprintAttack);
            HelpText(Tr(U8("允许当前武器在疾跑时开火和近战攻击，并保留本地疾跑速度；切换武器、打开菜单或关闭功能时恢复。联机实际移动与攻击仍以服务器判定为准。")));
            Title(Tr(U8("蓄力与装填")));
            DrawCustomCheckbox(Tr(U8("弓秒蓄力（木弓／复合弓／泰克复合弓）")), &g_Config::bFastBow, U8("缩短所选弓种的满蓄力时间；按住左键拉弓，松开出箭。下方可分别选择木弓、复合弓和泰克弓。"));
            if (auto Fade = ParameterFade("##InstantBowCharge", g_Config::bFastBow); Fade) {
                DrawChoiceGroup(U8("生效弓种"), {
                    {Tr(U8("木弓")), false, nullptr, &g_Config::bFastBowWood},
                    {Tr(U8("复合弓")), false, nullptr, &g_Config::bFastBowCompound},
                    {Tr(U8("泰克复合弓")), false, nullptr, &g_Config::bFastBowTek}
                });
                HelpText(Tr(U8("把当前手持弓的“满蓄力所需拉弓时间”压到最低，按下左键即接近满蓄力弹速；仍走原生松键出箭与服务器结算。泰克弓即时命中模式由蓝图直接结算，不受影响。")));
                DrawCustomCheckbox(Tr(U8("强制满蓄力直接出箭（仅本地主机）")), &g_Config::bFastBowForceShot);
                HelpText(Tr(U8("按下左键即调用原生开火流程直接出箭，跳过松键等待。只在本地主机（拥有 authority）生效：远程服务器收不到本机开火通知，故一律保留原生流程。仍按原生扣弹、冷却与命中结算。")));
                if (g_Config::bFastBowForceShot && !g_Hook::BowFullChargeOK)
                    ImGui::TextWrapped(Tr(U8("当前客户端未匹配武器开火接口，此项尚未生效。")));
                ImGui::TextWrapped(U8("%s"), g_Hack::GetInstantChargeStatus().c_str());
            }
            DrawCustomCheckbox(Tr(U8("本地主机换弹时长调整（实验）")), &g_Config::bInstantReload, U8("在本地主机调整所选武器的换弹时长，不自动补充弹药。远程服务器保持原生装填；泰克榴弹发射器选项仅提供只读诊断。"));
            if (auto Fade = ParameterFade("##InstantReload", g_Config::bInstantReload); Fade) {
                ImGui::PushID("InstantReloadFilters");
                DrawChoiceGroup(U8("生效武器类型"), {
                    {Tr(U8("泵动式霰弹枪")), false, nullptr, &g_Config::bInstantReloadPumpShotgun},
                    {Tr(U8("泰克榴弹发射器只读诊断")), false, nullptr, &g_Config::bInstantReloadLauncher},
                    {Tr(U8("制式步枪")), false, nullptr, &g_Config::bInstantReloadRifle},
                    {Tr(U8("制式弩")), false, nullptr, &g_Config::bInstantReloadCrossbow},
                    {Tr(U8("其他枪械")), false, nullptr, &g_Config::bInstantReloadOther}
                });
                HelpText(Tr(U8("仅本地主机修改换弹时长字段，实际装填效果待验证；不自动补满弹夹。远程秒换弹不可用，保持原生装填。泰克榴弹发射器仅提供只读诊断。")));
                DrawCustomCheckbox(Tr(U8("换弹不消耗弹药（仅本地主机）")), &g_Config::bInstantReloadProtectAmmo);
                HelpText(Tr(U8("只在拥有 authority 时下发：远程服务器自行扣减背包弹药，本地关闭扣弹会让弹药视图与服务器不一致。")));
                ImGui::TextWrapped(U8("%s"), g_Hack::GetInstantReloadStatus().c_str());
                if (g_Config::bInstantReloadLauncher) {
                    ImGui::TextDisabled("%s", Tr(U8("泰克榴弹发射器弹药槽（只读侦察）")));
                    ImGui::TextWrapped(U8("%s"), g_Hack::GetTekLauncherSlotStatus().c_str());
                    HelpText(Tr(U8("该武器属于 Genesis DLC 蓝图，SDK 内没有类声明，因此按运行时类名识别并反射读取弹药槽字段。此块只读，用于核对多弹种、逐槽装填与引爆模式字段。")));
                }
                ImGui::PopID();
            }
            Title(Tr(U8("泰克装备")));
            DrawCustomCheckbox(Tr(U8("泰克步枪不过热（狙击枪仅本地主机）")), &g_Config::bTekNoHeat);
            HelpText(Tr(U8("泰克步枪已验证可在远程服持续造成伤害。泰克狙击枪的热量由服务器维护，仅在本地主机拥有 authority 时可取消；远程服不会伪造本地连发。")));
            DrawCustomCheckbox(Tr(U8("泰克胸甲喷射冷却调整")), &g_Config::bTekChestJetNoCooldown);
            DrawCustomCheckbox(Tr(U8("泰克手套冲刺冷却调整")), &g_Config::bTekGloveDashNoCooldown);
            if (g_Config::bTekNoHeat || g_Config::bTekChestJetNoCooldown || g_Config::bTekGloveDashNoCooldown)
                ImGui::TextWrapped("%s", g_Hack::GetTekEquipmentStatus().c_str());
            Title(Tr(U8("骑乘与水下")));
            DrawCustomCheckbox(Tr(U8("骑乘持枪本地解锁")), &g_Config::bMountedWeapons);
            DrawCustomCheckbox(Tr(U8("骑乘持枪视角 360°")), &g_Config::bMountedFullRotation);
            DrawCustomCheckbox(Tr(U8("骑乘武器第三人称")), &g_Config::bMountedThirdPerson);
            HelpText(Tr(U8("骑乘选项解除当前武器与坐骑的本地限制，服务器仍可能拒绝装备或射击；需要先持有武器。弓箭蓄力修改仅在本地主机应用。")));
            ImGui::TextWrapped("%s", g_Hack::GetExpansionCombatStatus().c_str());
            DrawAnimatedSeparator();
            DrawCustomCheckbox(Tr(U8("水下使用枪械与泰克相枪")), &g_Config::bUnderwaterWeapons);
            HelpText(Tr(U8("对已加载的即时枪械和泰克相枪启用原生水下装备、开火标记；背包内已加载武器可在水下切换。关闭、换武器、死亡、换图或卸载时恢复修改。未加载的模组武器需先在陆地装备一次。保留弹药、冷却和命中流程；远程服务器仍可能拒绝开火或伤害，以实际扣弹和目标受伤为准。")));
            if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_3", g_Config::bUnderwaterWeapons); Fade) ImGui::TextWrapped("%s",g_Hack::GetUnderwaterWeaponStatus().c_str());
            DrawAnimatedSeparator();
                Title(Tr(U8("射速与弹道")));
                DrawCustomCheckbox(Tr(U8("制式弩按住速射（10ms，本地主机）")), &g_Config::bCrossbowHoldRapidFire);
                HelpText(Tr(U8("独立于射速倍率。本地主机按住左键时每 10ms 尝试原生射击；单发弹夹打空后调用原生弹药转移，直接从背包补箭，弹药用尽即停止。实际频率仍受帧率和武器状态限制。")));
                if (g_Config::bCrossbowHoldRapidFire && !g_Config::bWeaponFireRate)
                    ImGui::TextWrapped(U8("%s"), g_Hack::GetWeaponFireRateStatus().c_str());
                DrawCustomCheckbox(Tr(U8("枪械射速修改")), &g_Config::bWeaponFireRate, U8("调整所选枪械的本地开火间隔，倍率越大间隔越短。先选择生效武器类型，再调射速倍率；实际射击受服务器和武器状态限制。"));
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_4", g_Config::bWeaponFireRate); Fade) {
                    ImGui::PushID("FireRateFilters");
                    DrawCustomSliderFloat(Tr(U8("射速倍率")), &g_Config::WeaponFireRateMultiplier, 0.25f, 100.0f, "%.2f", 0.25f, "x");
                        DrawChoiceGroup(U8("生效枪械类型"), {
                        {Tr(U8("泰克步枪")), false, nullptr, &g_Config::bFireRateTekRifle},
                        {Tr(U8("制式步枪")), false, nullptr, &g_Config::bFireRateRifle},
                        {Tr(U8("霰弹枪")), false, nullptr, &g_Config::bFireRateShotgun},
                        {Tr(U8("狙击枪")), false, nullptr, &g_Config::bFireRateSniper},
                        {Tr(U8("手枪")), false, nullptr, &g_Config::bFireRatePistol},
                        {Tr(U8("弓（含泰克弓）")), false, nullptr, &g_Config::bFireRateBow},
                        {Tr(U8("其他枪械")), false, nullptr, &g_Config::bFireRateOther}
                    });
                    HelpText(Tr(U8("按当前武器的类名归类；制式弩由上方独立开关控制，不受射速倍率影响。未勾选的类型保持原生射速。")));
                    HelpText(Tr(U8("调整即时命中和投射物枪械的本地开火间隔。联机效果须核对实际扣弹和伤害；关闭、换枪或暂停时恢复。")));
                    ImGui::TextWrapped(U8("%s"), g_Hack::GetWeaponFireRateStatus().c_str());
                    ImGui::PopID();
                }
                DrawCustomCheckbox(Tr(U8("投射物速度／重力倍率（实验）")), &g_Config::bProjectileTrajectoryTuning);
                if (auto Fade=ParameterFade("##ProjectileTrajectoryTuning", g_Config::bProjectileTrajectoryTuning); Fade) {
                    DrawCustomSliderFloat(Tr(U8("投射物速度倍率")), &g_Config::ProjectileSpeedMultiplier, 0.25f, 5.0f, "%.2f", 0.05f, "x");
                    DrawCustomSliderFloat(Tr(U8("投射物重力倍率")), &g_Config::ProjectileGravityMultiplier, 0.05f, 2.0f, "%.2f", 0.05f, "x");
                    HelpText(Tr(U8("通过原生 GetProjectileSpeedModifier／GetProjectileGravityModifier 调整当前投射物武器；默认等同额外 1.5 倍弹速与 0.5 倍重力。会与武器已有反重力特性叠加。远程服务器是否接受须以服务器轨迹和伤害为准。")));
                    if (!g_Hook::ProjectileModifiersOK)
                        ImGui::TextWrapped(Tr(U8("当前客户端版本未匹配投射物倍率接口，功能保持关闭。")));
                }
                Title(Tr(U8("专用武器与自动射击")));
                DrawCustomCheckbox(Tr(U8("相枪按住左键保持目标")), &g_Config::bTekPistolHoldTarget);
                HelpText(Tr(U8("按住后不切换目标；死亡、遮挡或离开圆时暂停修正，松开后重新选取。")));
                DrawCustomCheckbox(Tr(U8("霰弹枪弹丸集中")), &g_Config::bShotgunConverge);
                HelpText(Tr(U8("取消普通／泵动霰弹枪的散布；没有锁定目标时也可单独使用。")));
                if (g_Config::bShotgunConverge && !g_Hook::ShotgunSpreadOK)
                    ImGui::TextWrapped(Tr(U8("当前客户端未匹配霰弹枪散布接口，此项尚未生效。")));
                DrawCustomCheckbox(Tr(U8("投掷物瞬间扔出（免举起、免蓄力）")), &g_Config::bInstantThrowables);
                HelpText(Tr(U8("手雷类投掷物装备时跳过举起动画，按下左键立即向准星方向扔出，不再蓄力等待松键。仍按原生流程消耗手雷并由服务器结算；关闭后恢复原生投掷。")));
                if (auto Fade = ParameterFade("##InstantThrowables", g_Config::bInstantThrowables); Fade)
                    DrawCustomCheckbox(Tr(U8("同时作用于泰克榴弹发射器")), &g_Config::bInstantThrowLauncher);
                if (g_Config::bInstantThrowables && !g_Hook::InstantThrowOK)
                    ImGui::TextWrapped(Tr(U8("当前客户端未匹配投掷物开火接口，此项尚未生效。")));
                ImGui::BeginDisabled(!g_Hook::ActorTickOK);
                DrawCustomCheckbox(Tr(U8("霰弹枪自动换弹")), &g_Config::bPumpShotgunNoReload);
                ImGui::EndDisabled();
                DrawCustomCheckbox(trigEnabled, &g_Config::bTriggerbotEnabled);
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_5", g_Config::bTriggerbotEnabled); Fade) DrawCustomSliderFloat(trigHitChance, &g_Config::TriggerHitChance, 0.0f, 100.0f, "%.0f", 1.0f, "%");
                DrawAnimatedSeparator();

        }
        if (IsTopic(Topic::Indicators)) {
                DrawColorPickerRow(Tr(U8("显示锁定范围圆")), &g_Config::bDrawAimCircle,
                    "AimCircleColor", g_Config::AimCircleColor);
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_6", g_Config::bDrawAimCircle); Fade)
                    DrawCustomSliderFloat(Tr(U8("圆线宽")), &g_Config::AimCircleThickness,
                        0.5f, 5.0f, "%.1f", 0.5f, "px");
                DrawColorPickerRow(Tr(U8("显示目标连线与标记")), &g_Config::bDrawAimTargetLine,
                    "AimTargetLineColor", g_Config::AimTargetLineColor);
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_7", g_Config::bDrawAimTargetLine); Fade) {
                    DrawCustomSliderFloat(Tr(U8("目标连线宽度")), &g_Config::AimTargetLineThickness,
                        0.5f, 5.0f, "%.1f", 0.5f, "px");
                    DrawCustomSliderFloat(Tr(U8("目标标记大小")), &g_Config::AimTargetMarkerSize,
                        0.0f, 20.0f, "%.0f", 1.0f, "px");
                }
                DrawAnimatedSeparator();

                DrawCustomCheckbox(Tr(U8("显示锁定目标名称与距离")), &g_Config::bDrawAimTargetInfo);
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_8", g_Config::bDrawAimTargetInfo); Fade)
                    DrawCustomSliderFloat(Tr(U8("目标信息字号")), &g_Config::AimTargetInfoScale,
                        0.5f, 2.0f, "%.1f", 0.1f, "x");


        }
        if (IsTopic(Topic::Spear)) {
                Title(Tr(U8("泰克长矛自动防御")));
                DrawCustomCheckbox(Tr(U8("锁定目标瞬间招架（右键短按）")), &g_Config::bTekSpearAutoBlock);
                DrawCustomCheckbox(Tr(U8("飞行物自动挥矛偏转（Shift + 右键）")), &g_Config::bTekSpearAutoDeflect);
                if (g_Config::bTekSpearAutoBlock || g_Config::bTekSpearAutoDeflect)
                DrawCustomCheckbox(Tr(U8("显示招架目标信息与飞行物状态")), &g_Config::bDrawTekSpearTargetInfo);
                ImGui::TextWrapped(U8("%s"), g_TekSpear::GetStatus().c_str());
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_9", g_Config::bTekSpearAutoBlock); Fade) {
                    g_Config::TekSpearTargetCount = g_TekSpear::TargetLimit(g_Config::TekSpearTargetCount);
                    ImGui::SliderInt(Tr(U8("锁定目标个数")), &g_Config::TekSpearTargetCount, 1, g_TekSpear::MaxLockedTargets, Tr(U8("%d 个")));
                        DrawChoiceGroup(U8("招架目标类型"), {
                        {Tr(U8("玩家")), false, nullptr, &g_Config::bSpearAimPlayers},
                        {Tr(U8("生物")), false, nullptr, &g_Config::bSpearAimCreatures},
                        {Tr(U8("Boss")), false, nullptr, &g_Config::bSpearAimBosses}
                    });
                    HelpText(Tr(U8("同部落玩家及其驯养生物始终视为友方，不会进入招架锁定；此处仅筛选敌对玩家、生物与 Boss。")));
                    DrawCustomCheckbox(Tr(U8("自动正对攻击者，招架后回正")), &g_Config::bTekSpearAutoFace);
                    DrawCustomSliderFloat(Tr(U8("招架目标锁定距离")), &g_Config::TekSpearTargetRangeMeters,
                        5.0f, 1000.0f, "%.0f", 5.0f, "m");
                    DrawCustomSliderFloat(Tr(U8("招架选取圆大小")), &g_Config::AimbotCircleRadiusPercent,
                        1.0f, 100.0f, "%.0f", 1.0f, "%");
                    HelpText(Tr(U8("与魔术子弹共用锁定圆大小；从圆内选择最近的 1–5 个有效单位，自动更新名单。")));
                    DrawColorPickerRow(Tr(U8("显示招架锁定圆")), &g_Config::bDrawTekSpearCircle,
                        "TekSpearTargetColor", g_Config::TekSpearTargetColor);
                    DrawCustomCheckbox(Tr(U8("显示招架目标连线与标记")), &g_Config::bDrawTekSpearTargetLine);
                    HelpText(Tr(U8("信息在圆心上方；线宽、标记大小和字号沿用上方设置，显示开关独立保存。")));
                    DrawCustomSliderFloat(Tr(U8("近战招架触发距离")), &g_Config::TekSpearMeleeRangeMeters,
                        1.0f, g_TekSpear::MaxMeleeRangeMeters, "%.1f", 0.5f, "m");
                    DrawCustomSliderFloat(Tr(U8("招架提前时间")), &g_Config::TekSpearParryLeadSeconds,
                        0.0f, g_TekSpear::MaxParryLeadSeconds, "%.2f", 0.01f, "s");
                    DrawCustomSliderFloat(Tr(U8("右键短按时长")), &g_Config::TekSpearParryTapSeconds,
                        0.03f, PhoenixInterface::MaxSpearTapSeconds, "%.2f", 0.01f, "s");
                    if (ImGui::Button(Tr(U8("重新选择格挡目标")))) g_TekSpear::RequestRetarget();
                    HelpText(Tr(U8("分别监测每个目标的打击时机，按攻击先后短按招架；同一时刻只能朝一个方向，完全同时从不同方向命中的攻击不保证全部挡住。")));
                    HelpText(Tr(U8("自动转向会在招架期间对准攻击者，结束或暂停时回正，并保留期间的鼠标转向。转向期间保留锁定组；关闭此项后由你控制朝向。这是实际转向招架。")));
                }
                if (auto Fade=ParameterFade("##parameters_Aimbot_Menu.cpp_10", g_Config::bTekSpearAutoDeflect); Fade) {
                    ImGui::TextWrapped(U8("%s"), g_TekSpear::GetProjectileStatus().c_str());
                    DrawCustomSliderFloat(Tr(U8("飞行物检测范围")), &g_Config::TekSpearProjectileRangeMeters,
                        5.0f, 150.0f, "%.0f", 5.0f, "m");
                    DrawCustomSliderFloat(Tr(U8("飞行物提前防御时间")), &g_Config::TekSpearLookAheadSeconds,
                        0.1f, 1.0f, "%.2f", 0.05f, "s");
                    HelpText(Tr(U8("飞行物独立检测，不受近战目标、锁定圆和视线朝向限制；按实际移动、碰撞大小及范围伤害预测接近，遮挡时暂停偏转。界面分别显示已识别、移动与预计接近的数量。")));
                }
                HelpText(Tr(U8("请使用默认右键／左 Shift 键位并面向目标。先按近战打击动画标记判断时机，未匹配时等待原生伤害窗口开启；不会因整段攻击动画而长按右键。")));
                HelpText(Tr(U8("未装备长矛或暂时遮挡仍可锁定和显示；目标须在近战范围内并朝你攻击。死亡、昏迷、友方、超距或出圈单位不参与选择；每个目标显示名称、距离及连线。持有可用长矛并锁定目标时暂停自动开火。")));
                HelpText(Tr(U8("菜单、背包、聊天、切出游戏或换武器时释放自动输入。手动右键优先，手动按住 Shift 时暂停近战自动格挡。")));
                HelpText(Tr(U8("需要长矛具备可用耐久及能量；防御状态确认不等于完全免伤。无实体飞行物的瞬时射击无法提前检测。")));

        }
        ImGui::EndDisabled();
        EndTabRegion(); EndMenuPage();
    }
}
