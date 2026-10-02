#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../Config/Configs.h"
#include "ConfigImGui.h"
#include "Visuals_Menu.h"
#include "FeatureCatalog.h"
#include "../../external/CppSDK/SDK.hpp"
#include "../Language/LanguageManager.h"
#include "../ESP/DrawESP.h"
#include "../Hack/Hack.h"

namespace g_DrawImGui {
    std::vector<const char*> ESP_Scale = {
        "75%",
        "100%",
        "125%",
        "150%",
        "200%"
    };

    std::vector<CustomComboItem> DroppedItemsCombo() { return {
        { LanguageManager::Visuals_Menu::DroppedItemPiled,            false, g_Config::DroppedItemPiledColor,            &g_Config::bDroppedItemPiled },
        { LanguageManager::Visuals_Menu::DroppedItemCryopod,          false, g_Config::DroppedItemCryopodColor,          &g_Config::bDroppedItemCryopod },
        { LanguageManager::Visuals_Menu::DroppedItemEgg,              false, g_Config::DroppedItemEggColor,              &g_Config::bDroppedItemEgg },
        { LanguageManager::Visuals_Menu::DroppedItemMeat,             false, g_Config::DroppedItemMeatColor,             &g_Config::bDroppedItemMeat },
        { LanguageManager::Visuals_Menu::DroppedItemSpoiledMeat,      false, g_Config::DroppedItemSpoiledMeatColor,      &g_Config::bDroppedItemSpoiledMeat },
        { LanguageManager::Visuals_Menu::DroppedItemWood,             false, g_Config::DroppedItemWoodColor,             &g_Config::bDroppedItemWood },
        { LanguageManager::Visuals_Menu::DroppedItemThatch,           false, g_Config::DroppedItemThatchColor,           &g_Config::bDroppedItemThatch },
        { LanguageManager::Visuals_Menu::DroppedItemMetal,            false, g_Config::DroppedItemMetalColor,            &g_Config::bDroppedItemMetal },
        { LanguageManager::Visuals_Menu::DroppedItemStone,            false, g_Config::DroppedItemStoneColor,            &g_Config::bDroppedItemStone },
        { LanguageManager::Visuals_Menu::DroppedItemCrystal,          false, g_Config::DroppedItemCrystalColor,          &g_Config::bDroppedItemCrystal },
        { LanguageManager::Visuals_Menu::DroppedItemGem,              false, g_Config::DroppedItemGemColor,              &g_Config::bDroppedItemGem },
        { LanguageManager::Visuals_Menu::DroppedItemPearl,            false, g_Config::DroppedItemPearlColor,            &g_Config::bDroppedItemPearl },
        { LanguageManager::Visuals_Menu::DroppedItemHide,             false, g_Config::DroppedItemHideColor,             &g_Config::bDroppedItemHide },
        { LanguageManager::Visuals_Menu::DroppedItemPelt,             false, g_Config::DroppedItemPeltColor,             &g_Config::bDroppedItemPelt },
        { LanguageManager::Visuals_Menu::DroppedItemKeratin,          false, g_Config::DroppedItemKeratinColor,          &g_Config::bDroppedItemKeratin },
        { LanguageManager::Visuals_Menu::DroppedItemChitin,           false, g_Config::DroppedItemChitinColor,           &g_Config::bDroppedItemChitin },
        { LanguageManager::Visuals_Menu::DroppedItemCorruptedPolymer, false, g_Config::DroppedItemCorruptedPolymerColor,  &g_Config::bDroppedItemCorruptedPolymer },
        { LanguageManager::Visuals_Menu::DroppedItemPolymer_Organic,  false, g_Config::DroppedItemPolymer_OrganicColor,  &g_Config::bDroppedItemPolymer_Organic },
        { LanguageManager::Visuals_Menu::DroppedItemPolymer,          false, g_Config::DroppedItemPolymerColor,          &g_Config::bDroppedItemPolymer }
    }; }

    static void DrawResourceHarvestControls() {
        if (IsTopic(Topic::Resources)) {
        Title(Tr(U8("资源识别与高亮")));
        DrawAnimatedSeparator();
        DrawCustomCheckbox(LanguageManager::Visuals_Menu::ResourceHighlight,
            &g_Config::bResourceESPEnabled);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_1", g_Config::bResourceESPEnabled); Fade) {
        DrawCustomSliderFloat(LanguageManager::Visuals_Menu::ResourceMaxDistance,
            &g_Config::ResourceESPMaxDistance, 25.0f, 1000.0f, "%.0f", 25.0f, "m");
        DrawCustomCheckbox(Tr(U8("资源模型变色（游戏原生轮廓）")),
            &g_Config::bResourceModelHighlight);
        DrawCustomCheckbox(Tr(U8("显示资源名称和距离")),
            &g_Config::bResourceShowLabels);

        }
        // Resource filters are shared by highlighting and auto harvesting, so
        // they must remain configurable when the visual highlight is disabled.
        DrawColorPickerRow(LanguageManager::Visuals_Menu::ResourceMetal,
            &g_Config::bResourceMetal, "ResourceMetalColor", g_Config::ResourceMetalColor);
        DrawColorPickerRow(LanguageManager::Visuals_Menu::ResourceCrystal,
            &g_Config::bResourceCrystal, "ResourceCrystalColor", g_Config::ResourceCrystalColor);
        DrawColorPickerRow(LanguageManager::Visuals_Menu::ResourceObsidian,
            &g_Config::bResourceObsidian, "ResourceObsidianColor", g_Config::ResourceObsidianColor);
        DrawColorPickerRow(LanguageManager::Visuals_Menu::ResourceOil,
            &g_Config::bResourceOil, "ResourceOilColor", g_Config::ResourceOilColor);
        DrawColorPickerRow(LanguageManager::Visuals_Menu::ResourcePearl,
            &g_Config::bResourcePearl, "ResourcePearlColor", g_Config::ResourcePearlColor);
        DrawColorPickerRow(LanguageManager::Visuals_Menu::ResourceElementShard,
            &g_Config::bResourceElementShard, "ResourceElementShardColor",
            g_Config::ResourceElementShardColor);

        DrawAnimatedSeparator();
        }
        if (IsTopic(Topic::Harvest)) {
            DrawCustomCheckbox(Tr(U8("骑乘生物自动采集资源")), &g_Config::bAutoHarvestResources);
            HelpText(Tr(U8("先选择资源类型，再骑乘可采集该资源的生物。搜索半径只决定找点范围；到达可采集站位后才攻击，持续有收益时保持目标。")));
            if (auto Fade=ParameterFade("##harvest_main",g_Config::bAutoHarvestResources); Fade) {
                ImGui::TextWrapped(Tr(U8("采集状态：%s")),g_Hack::GetAutoHarvestStatus().c_str());
                Title(Tr(U8("采集目标")));
                struct Filter { const char* Label; bool* Enabled; };
                const Filter Filters[]={{LanguageManager::Visuals_Menu::ResourceMetal,&g_Config::bResourceMetal},
                    {LanguageManager::Visuals_Menu::ResourceCrystal,&g_Config::bResourceCrystal},
                    {LanguageManager::Visuals_Menu::ResourceObsidian,&g_Config::bResourceObsidian},
                    {LanguageManager::Visuals_Menu::ResourceOil,&g_Config::bResourceOil},
                    {LanguageManager::Visuals_Menu::ResourcePearl,&g_Config::bResourcePearl},
                    {LanguageManager::Visuals_Menu::ResourceElementShard,&g_Config::bResourceElementShard}};
                if(ImGui::BeginTable("HarvestTargets",ImGui::GetContentRegionAvail().x>420?3:2)) {
                    for(const auto& F:Filters){ImGui::TableNextColumn();DrawCustomCheckbox(F.Label,F.Enabled);}
                    ImGui::EndTable();
                }
                if(DrawCustomButton(Tr(U8("资源配色与高亮")))) SelectTopic(Topic::Resources);
                DrawCustomSliderFloat(Tr(U8("自动采集搜索半径")),&g_Config::AutoHarvestSearchRadiusMeters,
                    5.f,PhoenixInterface::MaxHarvestSearchMeters,"%.0f",5.f,"m");
                HelpText(Tr(U8("只搜索客户端已加载、已识别的资源。扩大半径不会加载远处资源，也不会扩大服务器允许的命中距离。")));
                DrawAnimatedSeparator();
                Title(Tr(U8("采集方式")));
                DrawCustomCheckbox(Tr(U8("跨步者：自动靠近并使用采集光束")),&g_Config::bAutoHarvestTekStrider);
                if(auto Mode=ParameterFade("##harvest_strider",g_Config::bAutoHarvestTekStrider);Mode) {
                    DrawCustomSliderFloat(Tr(U8("跨步者采集光束距离")),&g_Config::AutoHarvestTekStriderBeamRangeMeters,
                        10.f,PhoenixInterface::MaxHarvestBeamMeters,"%.0f",5.f,"m");
                    HelpText(Tr(U8("需要采集钻头头部配件。此值控制寻路与开始采集的距离，不会直接改写原生光束长度；受阻或无收益时会调整站位。")));
                }
                if(auto Mode=ParameterFade("##harvest_ordinary",!g_Config::bAutoHarvestTekStrider);Mode) {
                    DrawCustomSliderFloat(Tr(U8("期望停靠距离")),&g_Config::AutoHarvestAttackDistanceMeters,
                        1.5f,PhoenixInterface::MaxHarvestApproachMeters,"%.1f",.5f,"m");
                    HelpText(Tr(U8("停靠距离会结合坐骑近战半径和采集收益修正，避免距离设大后原地空挥；此滑块本身不扩大攻击判定。")));
                    DrawCustomSliderFloat(Tr(U8("攻击间隔")),&g_Config::AutoHarvestAttackIntervalMs,
                        200.f,PhoenixInterface::MaxHarvestAttackIntervalMs,"%.0f",50.f,"ms");
                    DrawCustomCheckbox(Tr(U8("采集中扩大本地近战范围（实验）")),&g_Config::bDinoHarvestRange);
                    HelpText(Tr(U8("仅在自动采集期间调整当前普通坐骑可验证的基础近战半径，不处理投射物和跨步者。部分生物或模组会覆盖该值；服务器资源、命中与伤害仍由服务器确认。关闭采集、下坐骑或切换生物时恢复可核对的原值。")));
                    if(auto Range=ParameterFade("##harvest_range",g_Config::bDinoHarvestRange);Range) {
                        DrawCustomSliderFloat(Tr(U8("本地近战半径倍率")),&g_Config::DinoHarvestRangeMultiplier,
                            1.f,PhoenixInterface::MaxDinoHarvestRangeMultiplier,"%.2f",.1f,"x");
                        ImGui::TextWrapped("%s",g_Hack::GetDinoHarvestRangeStatus().c_str());
                    }
                    DrawCustomCheckbox(Tr(U8("自动靠近锁定的资源")),&g_Config::bAutoHarvestApproach);
                }
                if(auto Movement=ParameterFade("##harvest_movement",g_Config::bAutoHarvestTekStrider||g_Config::bAutoHarvestApproach);Movement) {
                    DrawAnimatedSeparator();Title(Tr(U8("移动与避障")));
                    DrawCustomCheckbox(Tr(U8("接近资源时自动疾跑（陆地）")),&g_Config::bAutoHarvestSprint);
                    DrawCustomCheckbox(Tr(U8("遇阻时尝试攻击石头和树木")),&g_Config::bAutoHarvestBreakObstacles);
                    DrawCustomCheckbox(Tr(U8("安全导航（陆地绕障碍并防止坠崖）")),&g_Config::bAutoHarvestSafeNavigation);
                    if(auto Safety=ParameterFade("##harvest_safety",g_Config::bAutoHarvestSafeNavigation);Safety) {
                        DrawCustomSliderFloat(Tr(U8("允许落差")),&g_Config::AutoHarvestMaxSafeDropMeters,
                            .5f,PhoenixInterface::MaxHarvestSafeDropMeters,"%.1f",.5f,"m");
                        DrawCustomCheckbox(Tr(U8("实验：智能寻路（候选落点与快速重规划）")),&g_Config::bAutoHarvestExperimentalNavigation);
                        HelpText(Tr(U8("优先保留有效路线，分批比较资源与站位；没有地图导航数据时使用地面和碰撞探测。允许落差越大，路线可接受的下落高度越高，请按坐骑承受能力设置。")));
                    }
                }
            }
            DrawAnimatedSeparator();Title(Tr(U8("背包清理")));
        DrawCustomCheckbox(Tr(U8("自动丢弃所选物资")), &g_Config::bAutoDropResources);
        HelpText(Tr(U8("仅处理人物背包，每次丢弃一整组所选物资；独立于骑乘采集。不会丢弃已装备、快捷栏、蓝图或印痕。金属仅指矿石，木头包含真菌木材，蘑菇包含稀有蘑菇，浆果不含种子。开启前先存好需要保留的材料；丢出后无法自动撤回。未选择类型时不执行，菜单、背包、上传面板或失焦时暂停。")));
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_2", g_Config::bAutoDropResources); Fade) {
            const char* Names[]={Tr(U8("金属矿")),Tr(U8("石头")),Tr(U8("浆果")),Tr(U8("蘑菇")),Tr(U8("水晶")),Tr(U8("纤维")),Tr(U8("茅草")),Tr(U8("木头")),Tr(U8("龙涎香")),Tr(U8("燧石")),Tr(U8("黑曜石"))};
            if (ImGui::BeginTable("DropMaterials", ImGui::GetContentRegionAvail().x>360?3:2)) {
                for (int I=0; I<11; ++I) {
                    ImGui::TableNextColumn(); bool Selected=(g_Config::AutoDropResourceMask & (1<<I))!=0;
                    if (DrawCustomCheckbox(Names[I], &Selected)) {
                        if (Selected) g_Config::AutoDropResourceMask |= 1<<I;
                        else g_Config::AutoDropResourceMask &= ~(1<<I);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TextWrapped("%s",g_Hack::GetAutoDropStatus().c_str());
        }
        DrawAnimatedSeparator();
        }
        if (IsTopic(Topic::Diagnostics)) {
        Title(Tr(U8("扫描诊断")));
        const g_DrawESP::ResourceESPDiagnostics Diagnostics =
            g_DrawESP::GetResourceESPDiagnostics();
        ImGui::TextDisabled(Tr(U8("扫描：关卡 %d | Actor %d | 实例组件 %d | 附近实例 %d | 已标记 %d | 模型 %d")),
            Diagnostics.Levels, Diagnostics.Actors, Diagnostics.InstanceComponents,
            Diagnostics.OverlappingInstances, Diagnostics.Markers,
            Diagnostics.HighlightedComponents);
        ImGui::TextDisabled(Tr(U8("模板 %d | 镜头通道 %s | PP材质 %d/%d | 调整器 %d | 缓存 %d")),
            Diagnostics.StencilValue,
            Diagnostics.CameraStencilEnabled ? Tr(U8("开")) : Tr(U8("关")),
            Diagnostics.CameraPPBlendables, Diagnostics.CameraPPBlendableCapacity,
            Diagnostics.CameraPPAdjusters, Diagnostics.CameraBlendCache);
        ImGui::TextDisabled(Tr(U8("CustomDepth %d | Nanite %d | 后处理 %s | 相机前置 %s | 渲染前写入 %d")),
            Diagnostics.CustomDepthMode, Diagnostics.NaniteCustomDepthMode,
            Diagnostics.PostProcessingEnabled ? Tr(U8("开")) : Tr(U8("关")),
            g_Hook::UpdateCameraOK ? Tr(U8("正常")) : Tr(U8("失败")),
            Diagnostics.PreRenderWrites);
        if (!Diagnostics.CameraPPMaterials.empty()) {
            ImGui::TextWrapped(U8("PP：%s"), Diagnostics.CameraPPMaterials.c_str());
        }
        if (!Diagnostics.OutlineMaterialStatus.empty()) {
            ImGui::TextWrapped(Tr(U8("描边材质：%s")),
                Diagnostics.OutlineMaterialStatus.c_str());
        }
    }

    }

    void ResourceHarvest_Menu() {
        if (BeginMenuPage(MenuPage::ResourceHarvest, Tr(U8("自动采集资源")))) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 14.0f));
            BeginTabRegion("ResourceHarvestRegion");
            ImGui::BeginDisabled(!g_Hook::PostRenderOK || !g_Hook::ActorTickOK);
            DrawResourceHarvestControls();
            ImGui::EndDisabled();
            EndTabRegion();
            ImGui::PopStyleVar();
            EndMenuPage();
        }
    }

    void Visuals_Menu() {
        if (!BeginMenuPage(MenuPage::Visuals, Tr(U8("透视显示")))) return;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 14.0f));
        BeginTabRegion("VisualsRegion");
        ImGui::BeginDisabled(!g_Hook::PostRenderOK);

        if (IsTopic(Topic::Overlay)) {
            DrawCustomCheckbox(Tr(U8("启用独立透视")), &g_Config::bOverlayESP);
            HelpText(Tr(U8("屏幕叠加绘制，不依赖游戏原生轮廓；以下参数独立保存。")));
            if (auto Fade = ParameterFade("##OverlayParameters", g_Config::bOverlayESP); Fade) {
                Title(Tr(U8("显示对象")));
                DrawCustomCheckbox(Tr(U8("人物")), &g_Config::bOverlayPlayers);
                DrawCustomCheckbox(Tr(U8("生物")), &g_Config::bOverlayCreatures);
                DrawCustomCheckbox(Tr(U8("建筑")), &g_Config::bOverlayStructures);
                DrawCustomCheckbox(Tr(U8("仅显示非友军")), &g_Config::bOverlayEnemyOnly);
                DrawAnimatedSeparator();
                Title(Tr(U8("骨骼与线框")));
                DrawCustomCheckbox(Tr(U8("人物／生物骨骼")), &g_Config::bOverlaySkeleton);
                DrawCustomCheckbox(Tr(U8("人物／生物方框")), &g_Config::bOverlayCharacterBox);
                DrawCustomCheckbox(Tr(U8("建筑三维线框")), &g_Config::bOverlayStructureBox);
                DrawCustomCheckbox(Tr(U8("名称")), &g_Config::bOverlayName);
                DrawCustomCheckbox(Tr(U8("距离")), &g_Config::bOverlayDistance);
                DrawCustomSliderFloat(Tr(U8("人物／生物显示距离")), &g_Config::OverlayCharacterDistance, 1, 2000, "%.0f", 10, "m");
                DrawCustomSliderFloat(Tr(U8("建筑显示距离")), &g_Config::OverlayStructureDistance, 1, 2000, "%.0f", 10, "m");
                DrawCustomSliderFloat(Tr(U8("独立透视线宽")), &g_Config::OverlayLineWidth, .5f, 5, "%.1f", .25f, "px");
                DrawCustomSliderFloat(Tr(U8("独立透视字号")), &g_Config::OverlayFontPixels, 10, 32, "%.0f", 1, "px");
                DrawMultiColorPicker(Tr(U8("非友军 / 友军 / 建筑")), true,
                    {{"##OverlayEnemy", g_Config::OverlayEnemyColor}, {"##OverlayTeam", g_Config::OverlayTeamColor}, {"##OverlayStructure", g_Config::OverlayStructureColor}});
                HelpText(Tr(U8("建筑线框为模型世界包围盒；不沿墙面描边。近处优先，骨骼每帧最多 32 个目标，只绘制有效投影线段。")));
            }
        }

        const auto SharedColorRow = [&](const char* Label, bool* Enabled, float* Enemy, float* Friendly) {
            ImGui::PushID(Label);
            DrawCustomCheckbox(Label, Enabled);
            if (*Enabled) {
                ImGui::SameLine();
                DrawMultiColorPicker(Tr(U8("敌方 / 友军")), true, {{"##Enemy", Enemy}, {"##Friendly", Friendly}});
            }
            ImGui::PopID();
        };
        if (IsTopic(Topic::Objects)) {
        DrawCustomCheckbox(Tr(U8("战斗模式（人物与炮塔弹量）")), &g_Config::bCombatESP);
        DrawCustomCheckbox(Tr(U8("简洁渲染（方框与血条）")), &g_Config::bMinimalESP);
        DrawColorPickerRow(Tr(U8("护甲品质")), &g_Config::bDrawArmorQuality, "ArmorQuality", g_Config::ArmorQualityColor);
        DrawColorPickerRow(Tr(U8("手持武器")), &g_Config::bDrawHeldWeapon, "HeldWeapon", g_Config::HeldWeaponColor);
        HelpText(Tr(U8("装备信息读取客户端已同步的数据；未同步时不显示。简洁模式隐藏文字，战斗模式保留炮塔弹量。")));
        Title(Tr(U8("显示对象")));
        DrawAnimatedSeparator();
        DrawCustomCheckbox(Tr(U8("玩家")), &g_Config::bESPPlayers);
        ImGui::SameLine();
        DrawCustomCheckbox(Tr(U8("生物")), &g_Config::bESPCreatures);
        ImGui::SameLine();
        DrawCustomCheckbox(Tr(U8("建筑")), &g_Config::bDrawStructures, U8("显示客户端已加载建筑的信息。设置显示距离，并可按建筑类型筛选；未加载建筑无法显示。"));
        HelpText(Tr(U8("勾选的对象共用功能开关；敌方与友军颜色分别设置。")));
        DrawCustomCheckbox(Tr(U8("生物敌意模型颜色")), &g_Config::bShowCreatureAggro);
        HelpText(Tr(U8("原生模型描边：白色平静、黄色逃离、红色敌意或敌方驯养；攻击你或坐骑时另显示文字。")));


        DrawAnimatedSeparator();
        Title(Tr(U8("模型与信息")));
        DrawCustomCheckbox(Tr(U8("显示原生模型")), &g_Config::bModelHighlightEnabled);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_7", g_Config::bModelHighlightEnabled); Fade) {
            HelpText(Tr(U8("原生模型保留敌方、友军各自的阵营配色。")));
            DrawCustomSliderFloat(Tr(U8("模型显示距离")), &g_Config::ModelHighlightMaxDistance,
                25.0f, 1000.0f, "%.0f", 25.0f, "m");
            DrawCustomSliderFloat(Tr(U8("模型颜色强度")), &g_Config::ModelHighlightIntensity,
                1.0f, 8.0f, "%.1f", 0.25f);
            DrawCustomSliderFloat(Tr(U8("模型不透明度")), &g_Config::ModelHighlightOpacity,
                0.25f, 1.0f, "%.2f", 0.05f);
        }
        SharedColorRow(Tr(U8("显示方框")), &g_Config::bDrawBox, g_Config::BoxColor, g_Config::BoxColorTeam);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_8", g_Config::bDrawBox); Fade) {
            DrawCustomCheckbox(Tr(U8("四角方框")), &g_Config::bESPCornerBox);
            DrawCustomSliderFloat(Tr(U8("方框边距")), &g_Config::ESPBoxPaddingPixels, 0.0f, 8.0f, "%.1f", 0.5f, "px");
            DrawCustomSliderFloat(Tr(U8("方框线宽")), &g_Config::ESPBoxThicknessPixels, 0.5f, 3.0f, "%.2f", 0.25f, "px");
        }
        SharedColorRow(Tr(U8("显示名称")), &g_Config::bDrawName, g_Config::NameColor, g_Config::NameColorTeam);
        SharedColorRow(Tr(U8("显示距离")), &g_Config::bDrawDistance, g_Config::DistanceColor, g_Config::DistanceColorTeam);
        DrawCustomCheckbox(Tr(U8("显示生命值")), &g_Config::bDrawHealthBar);
        DrawMultiColorPicker(Tr(U8("敌方满血 / 低血")), g_Config::bDrawHealthBar, {{"##SharedHealthFull", g_Config::HealthBarColor1}, {"##SharedHealthEmpty", g_Config::HealthBarColor2}});
        DrawMultiColorPicker(Tr(U8("友军满血 / 低血")), g_Config::bDrawHealthBar, {{"##TeamHealthFull", g_Config::TeamHealthColor1}, {"##TeamHealthEmpty", g_Config::TeamHealthColor2}});
        SharedColorRow(Tr(U8("显示眩晕值")), &g_Config::bDrawTorpor, g_Config::TorporColor, g_Config::TorporColorTeam);
        SharedColorRow(Tr(U8("显示尸体信息")), &g_Config::bDrawRagdoll, g_Config::RagdollColor, g_Config::RagdollColorTeam);
        HelpText(Tr(U8("建筑共用模型、方框、名称、距离和生命值设置；眩晕与尸体信息仅用于玩家和生物。")));
        HelpText(Tr(U8("模型与信息开关各自独立；只看模型时关闭方框、名称等信息即可。")));

        DrawAnimatedSeparator();
        }
        if (IsTopic(Topic::Typography)) {
        Title(Tr(U8("信息大小")));
        DrawCustomSliderFloat(Tr(U8("信息字号")), &g_Config::ESPFontPixels, 12.0f, PhoenixInterface::MaxFontPixels, "%.0f", 1.0f, "px");
        DrawCustomSliderFloat(Tr(U8("物品图标大小")), &g_Config::ESPIconPixels, 10.0f, 40.0f, "%.0f", 1.0f, "px");
        DrawCustomSliderFloat(Tr(U8("最小字号")), &g_Config::ESPMinFontPixels, 12.0f, PhoenixInterface::MaxFontPixels, "%.0f", 1.0f, "px");
        g_Config::ESPMinFontPixels = std::min(g_Config::ESPMinFontPixels, g_Config::ESPFontPixels);
        DrawCustomCombo(Tr(U8("整体文字缩放")), &g_Config::ESPScaleIdx, ESP_Scale);
        DrawCustomCheckbox(Tr(U8("信息防重叠")), &g_Config::bESPPreventOverlap);
        HelpText(Tr(U8("名称、距离与状态条按目标组合显示。拥挤时优先保留锁定目标和近处生物；其余信息尝试放在方框四周，仍无空间时隐藏整组。")));
        HelpText(Tr(U8("最小字号不超过信息字号；远处文字保持可读大小。")));
        DrawCustomCheckbox(Tr(U8("文字随距离近大远小")), &g_Config::bDinoDistanceTextScale);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_9", g_Config::bDinoDistanceTextScale); Fade) {
            DrawCustomSliderFloat(Tr(U8("近距离文字比例")), &g_Config::DinoNearTextScale, 0.75f, 2.0f, "%.2f", 0.05f);
            DrawCustomSliderFloat(Tr(U8("远距离文字比例")), &g_Config::DinoFarTextScale, 0.50f, 1.25f, "%.2f", 0.05f);
            DrawCustomSliderFloat(Tr(U8("远距离比例生效距离")), &g_Config::DinoTextScaleDistance, 25.0f, 1000.0f, "%.0f", 25.0f, "m");
        }

        DrawAnimatedSeparator();
        }
        if (IsTopic(Topic::Structures)) {
        DrawColorPickerRow(Tr(U8("炮塔弹量")), &g_Config::bDrawTurretAmmo, "TurretAmmo", g_Config::TurretAmmoColor);
        Title(Tr(U8("建筑筛选")));
        DrawCustomCheckbox(Tr(U8("显示建筑")), &g_Config::bDrawStructures, U8("显示客户端已加载建筑的信息。设置显示距离，并可按建筑类型筛选；未加载建筑无法显示。"));
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_10", g_Config::bDrawStructures); Fade) {
            DrawCustomSliderFloat(Tr(U8("建筑显示距离")), &g_Config::StructureMaxDistance, 1.0f, 10000.0f, "%.0f", 1.0f, "m");
            DrawColorPickerRow(Tr(U8("名称下显示建筑归属")), &g_Config::bStructureShowOwner, "StructureOwnerCol", g_Config::StructureOwnerColor);
            DrawCustomCheckbox(Tr(U8("按建筑类型筛选")), &g_Config::bStructureTypeFilter, U8("按所选类型过滤建筑显示；全不选将隐藏所有建筑。关闭此筛选时不限制建筑类型，名称筛选仍生效。"));
            if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_11", g_Config::bStructureTypeFilter); Fade) {
                DrawChoiceGroup(U8("建筑类型"), {
                    {Tr(U8("自动炮台")), false, nullptr, &g_Config::bStructureAutoTurret},
                    {Tr(U8("重型自动炮台")), false, nullptr, &g_Config::bStructureHeavyTurret},
                    {Tr(U8("泰克炮台")), false, nullptr, &g_Config::bStructureTekTurret},
                    {Tr(U8("发电机")), false, nullptr, &g_Config::bStructureGenerator},
                    {Tr(U8("泰克发电机")), false, nullptr, &g_Config::bStructureTekGenerator},
                    {Tr(U8("风力发电机")), false, nullptr, &g_Config::bStructureWindTurbine},
                    {Tr(U8("补给箱／空投")), false, nullptr, &g_Config::bStructureSupplyDrop},
                    {Tr(U8("洞穴／海底战利品箱")), false, nullptr, &g_Config::bStructureLootCrate},
                    {Tr(U8("藏宝箱／埋藏宝箱")), false, nullptr, &g_Config::bStructureBuriedTreasure},
                    {Tr(U8("其他建筑")), false, nullptr, &g_Config::bStructureOther}
                });
                HelpText(Tr(U8("仅显示勾选类型；全部取消则不显示建筑。建筑列表的名称筛选同时生效。")));
            }
            DrawCustomCheckbox(Tr(U8("显示已同步的箱内物品")), &g_Config::bStructureShowLootContents);
            HelpText(Tr(U8("箱内物品按紧凑网格显示；BP 表示蓝图，? 表示类型未知。无图标时显示名称，最多展示 24 项，其余显示数量。")));
            HelpText(Tr(U8("优先显示客户端已同步物品；装有 A 镜且正查看箱子时，也读取其客户端返回，短暂显示在建筑标签中。")));
            HelpText(Tr(U8("建筑变色还受上方模型显示距离限制；关闭名称时一并隐藏归属。")));
        }
        else HelpText(Tr(U8("在上方显示对象中勾选建筑后设置筛选。")));
        DrawAnimatedSeparator();
        }
        if (IsTopic(Topic::World)) {
        DrawCustomCheckbox(Tr(U8("雷达显示")), &g_Config::bRadar);
        if (auto Fade = ParameterFade("##RadarParameters", g_Config::bRadar); Fade) {
            DrawCustomSliderFloat(Tr(U8("雷达范围")), &g_Config::RadarRangeMeters, 10, 1000, "%.0f", 10, "m");
            DrawCustomSliderFloat(Tr(U8("雷达尺寸")), &g_Config::RadarSizePixels, 100, 360, "%.0f", 10, "px");
            DrawMultiColorPicker(Tr(U8("雷达人物 / 恐龙 / 友军")), true, {{"##RadarPlayers",g_Config::RadarPlayerColor},{"##RadarDinos",g_Config::RadarDinoColor},{"##RadarTeam",g_Config::RadarTeamColor}});
            HelpText(Tr(U8("雷达位于右上角，上方为当前视角朝向；使用人物与生物筛选，最多显示 512 个已加载目标。")));
        }
        Title(Tr(U8("物品与水源")));
        DrawColorPickerRow(Tr(U8("显示掉落物")), &g_Config::bDrawDroppedItems, "DroppedItemNameCol", g_Config::DroppedItemNameColor);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_12", g_Config::bDrawDroppedItems); Fade) {
            auto dropped_items_combo = DroppedItemsCombo();
            DrawCustomMultiCombo(Tr(U8("##掉落物过滤")), dropped_items_combo, LanguageManager::Visuals_Menu::DroppedItemsCombo);
        }
        DrawColorPickerRow(Tr(U8("显示水源")), &g_Config::bDrawWater, "WaterNameCol", g_Config::WaterNameColor);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_13", g_Config::bDrawWater); Fade)
            DrawCustomSliderFloat(Tr(U8("水源显示数量")), &g_Config::WaterMaxCount, 1.0f, 10.0f, "%.0f", 1.0f);

        DrawAnimatedSeparator();
        Title(Tr(U8("视野外提示")));
        DrawColorPickerRow(Tr(U8("显示视野外的威胁")), &g_Config::bEnableOOF, "OOFCol1", g_Config::OOFColor);
        if (auto Fade=ParameterFade("##parameters_Visuals_Menu.cpp_14", g_Config::bEnableOOF); Fade) {
            DrawCustomSliderFloat(Tr(U8("箭头尺寸")), &g_Config::OOFSize, 5.0f, 30.0f, "%.1f", 1.0f);
            DrawCustomSliderFloat(Tr(U8("屏幕半径")), &g_Config::OOFRadius, 0.5f, 1.0f, "%.2f", 0.01f);
            DrawCustomSliderFloat(Tr(U8("威胁最大距离")), &g_Config::OOFMaxDistance, 25.0f, 2000.0f, "%.0f", 25.0f, "m");
            if (!g_Config::bStaticThreatIndicators) DrawCustomSliderFloat(Tr(U8("呼吸速度")), &g_Config::OOFBreathSpeed, 0.1f, 5.0f, "%.1f", 0.1f);
            DrawCustomSliderFloat(Tr(U8("最小透明度")), &g_Config::OOFMinAlpha, 0.1f, 0.9f, "%.2f", 0.01f);
            DrawCustomSliderFloat(Tr(U8("最大透明度")), &g_Config::OOFMaxAlpha, 0.2f, 1.0f, "%.2f", 0.01f);
        }
        }
        ImGui::EndDisabled();
        EndTabRegion();
        ImGui::PopStyleVar();
        EndMenuPage();
    }
}
