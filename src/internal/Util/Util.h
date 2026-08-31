#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../../external/Minimal-D3D12-Hook-ImGui/MinHook/include/MinHook.h"
#include "../../external/AOBScan/AOBScan.hpp"
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"
#include "../Config/Configs.h"
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <numbers>
#include <cwctype> 
#include <shellapi.h> 

namespace g_Util {
    static const float inv255 = 1.0f / 255.0f; 

    __forceinline const char* GetKeyName(UINT vk) {
        if (vk >= 'A' && vk <= 'Z') {
            static char buf[2] = { 0 };
            buf[0] = (char)vk;
            return buf;
        }
        if (vk >= '0' && vk <= '9') {
            static char buf[2] = { 0 };
            buf[0] = (char)vk;
            return buf;
        }
        switch (vk) {
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
        case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
        case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        case VK_INSERT: return "INSERT"; case VK_DELETE: return "DELETE"; case VK_HOME: return "HOME";
        case VK_END: return "END"; case VK_PRIOR: return "PAGE UP"; case VK_NEXT: return "PAGE DOWN";
        case VK_TAB: return "TAB"; case VK_LSHIFT: return "LSHIFT"; case VK_RSHIFT: return "RSHIFT";
        case VK_LCONTROL: return "LCTRL"; case VK_RCONTROL: return "RCTRL"; case VK_LMENU: return "LALT";
        case VK_RMENU: return "RALT"; case VK_CAPITAL: return "CAPSLOCK"; case VK_SPACE: return "SPACE";
        case VK_RETURN: return "ENTER"; case VK_ESCAPE: return "ESCAPE";
        default: return "UNKNOWN";
        }
    }

    __forceinline std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return std::tolower(c);
            });
        return s;
    }

    __forceinline ImU32 GetU32Color(float color[4]) {
        return ImGui::ColorConvertFloat4ToU32(*(ImVec4*)color);
    }

    __forceinline ImU32 ToImColor(float r, float g, float b, float a) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r * inv255, g * inv255, b * inv255, a * inv255));
    }

    __forceinline ImU32 GetHealthColor(float healthPercent) {
        healthPercent = fmaxf(0.0f, fminf(healthPercent, 1.0f));

        float r = fminf(1.0f, 2.0f - 2.0f * healthPercent);
        float g = fminf(1.0f, 2.0f * healthPercent);

        return ToImColor(r * 255.0f, g * 255.0f, 0.0f, 255.0f);
    }

    __forceinline std::string NormalizeAutoFeedMeatIdentity(std::string Value) {
        Value.erase(std::remove_if(Value.begin(), Value.end(), [](unsigned char Character) {
            return !std::isalnum(Character);
            }), Value.end());
        std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character) {
            return static_cast<char>(std::tolower(Character));
            });
        return Value;
    }

    __forceinline int GetAutoFeedMeatPriority(SDK::UPrimalItem* Item) {
        if (!Item || !Item->Class || Item->ItemQuantity <= 0 ||
            Item->bIsBlueprint || Item->bIsEngram || Item->bIsItemSkin) {
            return -1;
        }

        // Stack mods commonly replace the stock item class. Build a stable
        // identity from the replacement class, all parent classes, object
        // paths and the localized display name (often containing an English
        // token such as "[RAW MUTTON]").
        std::string Identity;
        SDK::UClass* Type = Item->Class;
        for (int Depth = 0; Type && Depth < 32; ++Depth) {
            Identity += Type->GetName();
            Identity += ' ';
            Type = static_cast<SDK::UClass*>(Type->SuperStruct);
        }
        Identity += Item->GetName();
        Identity += ' ';
        Identity += Item->GetFullName();
        Identity += ' ';
        Identity += Item->DescriptiveNameBase.ToString();
        Identity = NormalizeAutoFeedMeatIdentity(std::move(Identity));

        // Keep the whitelist narrow: similar-looking jerky and spoiled meat
        // must not be consumed by this feature.
        if (Identity.find("jerky") != std::string::npos ||
            Identity.find("spoiledmeat") != std::string::npos) {
            return -1;
        }

        // Prefer ordinary meat and preserve prime meat/mutton until they are
        // the only supported food left.
        if (Identity.find("cookedlambchop") != std::string::npos) return 6;
        if (Identity.find("rawmutton") != std::string::npos) return 7;
        if (Identity.find("cookedprimemeat") != std::string::npos) return 4;
        if (Identity.find("rawprimemeat") != std::string::npos) return 5;
        if (Identity.find("cookedfishmeat") != std::string::npos) return 2;
        if (Identity.find("rawfishmeat") != std::string::npos) return 3;
        if (Identity.find("cookedmeat") != std::string::npos) return 0;
        if (Identity.find("rawmeat") != std::string::npos) return 1;
        return -1;
    }

    __forceinline bool IsEntityMatch(std::string displayName, std::string filter) {
        if (filter.empty()) return true;

        std::string nameLower = g_Util::ToLower(displayName);
        std::string filterLower = g_Util::ToLower(filter);

        size_t lastPos = 0;
        for (size_t j = 0; j < filterLower.length(); ) {
            unsigned char c = static_cast<unsigned char>(filterLower[j]);
            int charLen = 1;
            if (c >= 0xf0) charLen = 4;
            else if (c >= 0xe0) charLen = 3;
            else if (c >= 0xc0) charLen = 2;

            std::string sub = filterLower.substr(j, charLen);
            size_t foundPos = nameLower.find(sub, lastPos);

            if (foundPos == std::string::npos) return false;

            lastPos = foundPos + charLen;
            j += charLen;
        }
        return true;
    }

    __forceinline static std::vector<std::string> SplitFilterTokens(const std::string& filter) {
        std::vector<std::string> tokens;
        std::stringstream ss(filter);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                tokens.push_back(token.substr(start, end - start + 1));
            }
            else if (start != std::string::npos) {
                tokens.push_back(token.substr(start));
            }
        }
        return tokens;
    }

    __forceinline static bool IsEntityMatchMulti(const std::string& displayName, const char* filterBuf) {
        std::string filter = filterBuf;
        if (filter.empty()) return true;

        std::vector<std::string> tokens = SplitFilterTokens(filter);
        if (tokens.empty()) return true;

        for (const std::string& token : tokens) {
            if (token.empty()) continue;
            if (g_Util::IsEntityMatch(displayName, token.c_str())) return true;
        }
        return false;
    }

    __forceinline bool IsStructureMatch(const std::string& structureName, const std::string& filter) {
        if (filter.empty()) return true;

        std::string lowerName = structureName;
        std::string lowerFilter = filter;

        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

        return lowerName.find(lowerFilter) != std::string::npos;
    }

    __forceinline static bool IsStructureMatchMulti(const std::string& structureName, const char* filterBuf) {
        std::string filter = filterBuf;
        if (filter.empty()) return true;

        std::vector<std::string> tokens = SplitFilterTokens(filter);
        if (tokens.empty()) return true;

        for (const std::string& token : tokens) {
            if (token.empty()) continue;
            if (g_Util::IsStructureMatch(structureName, token.c_str())) return true;
        }
        return false;
    }

    __forceinline SDK::APlayerController* GetLocalPC() {
        SDK::UWorld* World = SDK::UWorld::GetWorld();

        if (!World) {
            return nullptr;
        }

        if (!World->OwningGameInstance) {
            return nullptr;
        }

        if (World->OwningGameInstance->LocalPlayers.Num() == 0) {
            return nullptr;
        }

        auto LocalPlayer = World->OwningGameInstance->LocalPlayers[0];
        if (!LocalPlayer) {
            return nullptr;
        }

        auto PC = LocalPlayer->PlayerController;
        if (!PC) {
            return nullptr;
        }

        return PC;
    }

    __forceinline float GetAngleDistance(SDK::FVector CamLoc, SDK::FVector TargetLoc, SDK::FRotator CamRot) {
        SDK::FVector Diff = { TargetLoc.X - CamLoc.X, TargetLoc.Y - CamLoc.Y, TargetLoc.Z - CamLoc.Z };
        SDK::FVector DirToTarget = SDK::UKismetMathLibrary::Normal(Diff, 0.0001f);
        SDK::FVector CamForward = SDK::UKismetMathLibrary::GetForwardVector(CamRot);
        float Dot = SDK::UKismetMathLibrary::Dot_VectorVector(DirToTarget, CamForward);
        Dot = SDK::UKismetMathLibrary::FClamp(Dot, -1.0f, 1.0f);
        return SDK::UKismetMathLibrary::DegAcos(Dot);
    }

    __forceinline void MimicMouseClick(bool bPress) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = bPress ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    __forceinline bool ProcessDinoFeed(SDK::AShooterPlayerController* PC, SDK::APrimalDinoCharacter* Dino) {
        if (!PC || !Dino || Dino->IsDead()) return false;

        SDK::UPrimalInventoryComponent* Inv = Dino->MyInventoryComponent;
        if (!Inv) return false;

        SDK::TArray<SDK::UPrimalItem*>& Items = Inv->InventoryItems;
        SDK::UPrimalItem* BestMeat = nullptr;
        int BestPriority = 100;

        for (int i = 0; i < Items.Num(); i++) {
            SDK::UPrimalItem* Item = Items[i];
            const int Priority = GetAutoFeedMeatPriority(Item);
            if (Priority >= 0 && Priority < BestPriority) {
                BestMeat = Item;
                BestPriority = Priority;
                if (Priority == 0) break;
            }
        }

        if (BestMeat) {
            PC->ServerRequestInventoryUseItem(Inv, BestMeat->ItemID, BestMeat->SlotIndex);
            return true;
        }
        return false;
    }

    __forceinline static std::string IntToStr(int v)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", v);
        return buf; // NRVO / SSO，比 std::to_string 开销低
    }

    __forceinline static float ApproachAlpha(float cur, float target, float deltaSeconds, float fadeTime)
    {
        if (fadeTime <= 0.0f) return target;
        float diff = target - cur;
        float maxStep = deltaSeconds / fadeTime;
        if (fabsf(diff) <= maxStep) return target;
        return cur + (diff > 0.0f ? maxStep : -maxStep);
    }

    __forceinline static ImU32 ResolveDroppedItemColor(const std::string& className, float itemRating, int quantity)
    {
        const char* cn = className.c_str();

        if (strstr(cn, "PrimalItem_WeaponEmptyCryopod"))
            return g_Util::GetU32Color(g_Config::DroppedItemCryopodColor);
        if (strstr(cn, "Egg"))
            return g_Util::GetU32Color(g_Config::DroppedItemEggColor);
        if (quantity >= 1000)
            return g_Util::GetU32Color(g_Config::DroppedItemPiledColor);
        if (strstr(cn, "PrimalItemResource_FungalWood") || strstr(cn, "PrimalItemResource_Wood"))
            return g_Util::GetU32Color(g_Config::DroppedItemWoodColor);
        if (strstr(cn, "PrimalItemResource_Thatch"))
            return g_Util::GetU32Color(g_Config::DroppedItemThatchColor);
        if (strstr(cn, "PrimalItemResource_Hide"))
            return g_Util::GetU32Color(g_Config::DroppedItemHideColor);
        if (strstr(cn, "PrimalItemResource_Pelt"))
            return g_Util::GetU32Color(g_Config::DroppedItemPeltColor);
        if (strstr(cn, "PrimalItemResource_Keratin"))
            return g_Util::GetU32Color(g_Config::DroppedItemKeratinColor);
        if (strstr(cn, "PrimalItemResource_Chitin"))
            return g_Util::GetU32Color(g_Config::DroppedItemChitinColor);
        if (strstr(cn, "PrimalItemResource_CorruptedPolymer"))
            return g_Util::GetU32Color(g_Config::DroppedItemCorruptedPolymerColor);
        if (strstr(cn, "PrimalItemResource_Polymer_Organic"))
            return g_Util::GetU32Color(g_Config::DroppedItemPolymer_OrganicColor);
        if (strstr(cn, "PrimalItemResource_Polymer"))
            return g_Util::GetU32Color(g_Config::DroppedItemPolymerColor);
        if (strstr(cn, "PrimalItemResource_ScrapMetalIngot") ||
            strstr(cn, "PrimalItemResource_MetalIngot") ||
            strstr(cn, "PrimalItemResource_ScrapMetal") ||
            strstr(cn, "PrimalItemResource_Metal"))
            return g_Util::GetU32Color(g_Config::DroppedItemMetalColor);
        if (strstr(cn, "PrimalItemResource_Stone"))
            return g_Util::GetU32Color(g_Config::DroppedItemStoneColor);
        if (strstr(cn, "PrimalItemResource_Crystal"))
            return g_Util::GetU32Color(g_Config::DroppedItemCrystalColor);
        if (strstr(cn, "PrimalItemResource_Gem_Fertile") ||
            strstr(cn, "PrimalItemResource_Gem_BioLum") ||
            strstr(cn, "PrimalItemResource_Gem_Element") ||
            strstr(cn, "PrimalItemResource_BlueSap") ||
            strstr(cn, "PrimalItemResource_RedSap"))
            return g_Util::GetU32Color(g_Config::DroppedItemGemColor);
        if (strstr(cn, "PrimalItemResource_Silicon") ||
            strstr(cn, "PrimalItemResource_BlackPearl"))
            return g_Util::GetU32Color(g_Config::DroppedItemPearlColor);
        if (strstr(cn, "PrimalItemConsumable_SpoiledMeat"))
            return g_Util::GetU32Color(g_Config::DroppedItemSpoiledMeatColor);
        if (strstr(cn, "PrimalItemConsumable_RawMeat") ||
            strstr(cn, "PrimalItemConsumable_RawPrimeMeat") ||
            strstr(cn, "PrimalItemConsumable_RawMutton") ||
            strstr(cn, "PrimalItemConsumable_RawPrimeMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_RawMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_CookedMeat") ||
            strstr(cn, "PrimalItemConsumable_CookedPrimeMeat") ||
            strstr(cn, "PrimalItemConsumable_CookedLambChop") ||
            strstr(cn, "PrimalItemConsumable_CookedPrimeMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_CookedMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_CookedMeat_Jerky") ||
            strstr(cn, "PrimalItemConsumable_CookedPrimeMeat_Jerky"))
            return g_Util::GetU32Color(g_Config::DroppedItemMeatColor);

        // 按品质评级上色
        if (itemRating >= 10.0f) return g_Util::ToImColor(0, 255, 255, 255);
        if (itemRating >= 7.0f) return g_Util::ToImColor(255, 255, 0, 255);
        if (itemRating >= 4.5f) return g_Util::ToImColor(160, 32, 240, 255);
        if (itemRating >= 2.5f) return g_Util::ToImColor(0, 191, 255, 255);
        if (itemRating >= 1.25f) return g_Util::ToImColor(50, 205, 50, 255);

        return g_Util::GetU32Color(g_Config::DroppedItemNameColor);
    }

    __forceinline bool IsDroppedItemAllowed(const std::string& className, int quantity)
    {
        const char* cn = className.c_str();

        if (strstr(cn, "PrimalItem_WeaponEmptyCryopod"))
            return g_Config::bDroppedItemCryopod;
        if (strstr(cn, "Egg"))
            return g_Config::bDroppedItemEgg;
        if (quantity >= 1000)
            return g_Config::bDroppedItemPiled;
        if (strstr(cn, "PrimalItemResource_FungalWood") || strstr(cn, "PrimalItemResource_Wood"))
            return g_Config::bDroppedItemWood;
        if (strstr(cn, "PrimalItemResource_Thatch"))
            return g_Config::bDroppedItemThatch;
        if (strstr(cn, "PrimalItemResource_Hide"))
            return g_Config::bDroppedItemHide;
        if (strstr(cn, "PrimalItemResource_Pelt"))
            return g_Config::bDroppedItemPelt;
        if (strstr(cn, "PrimalItemResource_Keratin"))
            return g_Config::bDroppedItemKeratin;
        if (strstr(cn, "PrimalItemResource_Chitin"))
            return g_Config::bDroppedItemChitin;
        if (strstr(cn, "PrimalItemResource_CorruptedPolymer"))
            return g_Config::bDroppedItemCorruptedPolymer;
        if (strstr(cn, "PrimalItemResource_Polymer_Organic"))
            return g_Config::bDroppedItemPolymer_Organic;
        if (strstr(cn, "PrimalItemResource_Polymer"))
            return g_Config::bDroppedItemPolymer;
        if (strstr(cn, "PrimalItemResource_ScrapMetalIngot") ||
            strstr(cn, "PrimalItemResource_MetalIngot") ||
            strstr(cn, "PrimalItemResource_ScrapMetal") ||
            strstr(cn, "PrimalItemResource_Metal"))
            return g_Config::bDroppedItemMetal;
        if (strstr(cn, "PrimalItemResource_Stone"))
            return g_Config::bDroppedItemStone;
        if (strstr(cn, "PrimalItemResource_Crystal"))
            return g_Config::bDroppedItemCrystal;
        if (strstr(cn, "PrimalItemResource_Gem_Fertile") ||
            strstr(cn, "PrimalItemResource_Gem_BioLum") ||
            strstr(cn, "PrimalItemResource_Gem_Element") ||
            strstr(cn, "PrimalItemResource_BlueSap") ||
            strstr(cn, "PrimalItemResource_RedSap"))
            return g_Config::bDroppedItemGem;
        if (strstr(cn, "PrimalItemResource_Silicon") ||
            strstr(cn, "PrimalItemResource_BlackPearl"))
            return g_Config::bDroppedItemPearl;
        if (strstr(cn, "PrimalItemConsumable_SpoiledMeat"))
            return g_Config::bDroppedItemSpoiledMeat;
        if (strstr(cn, "PrimalItemConsumable_RawMeat") ||
            strstr(cn, "PrimalItemConsumable_RawPrimeMeat") ||
            strstr(cn, "PrimalItemConsumable_RawMutton") ||
            strstr(cn, "PrimalItemConsumable_RawPrimeMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_RawMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_CookedMeat") ||
            strstr(cn, "PrimalItemConsumable_CookedPrimeMeat") ||
            strstr(cn, "PrimalItemConsumable_CookedLambChop") ||
            strstr(cn, "PrimalItemConsumable_CookedPrimeMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_CookedMeat_Fish") ||
            strstr(cn, "PrimalItemConsumable_CookedMeat_Jerky") ||
            strstr(cn, "PrimalItemConsumable_CookedPrimeMeat_Jerky"))
            return g_Config::bDroppedItemMeat;

        // 如果没有被上述特定分类命中，默认允许绘制（例如武器、装备等普通物品）
        return true;
    }

    __forceinline void* FindUFunction(std::string FuncName) {
        void* Address = nullptr;
        SDK::UObject* Obj = nullptr;
        SDK::UFunction* Func = nullptr;

        Obj = SDK::UObject::FindObject(FuncName);
        if (Obj && Obj->IsA(SDK::UFunction::StaticClass())) {
            Func = static_cast<SDK::UFunction*>(Obj);
        }

        if (Func && Func->ExecFunction) {
            Address = reinterpret_cast<void*>(Func->ExecFunction);
        }

        return Address;
    }

    __forceinline void PotatoGraphics(SDK::UObject* WorldContextObject, SDK::UGameViewportClient* GameViewportClient) {
        if (!WorldContextObject) return;
        if (!GameViewportClient) return;

        // 禁用后处理
        // [2] -> (ByteOffset: +0x0, Bit: 2)
        GameViewportClient->EngineShowFlags[0] &= ~(1 << 2);

        // 地面材质降低
        // [61] -> (ByteOffset: +0x7, Bit: 5)
        GameViewportClient->EngineShowFlags[0x7] &= ~(1 << 5);

        // 删掉远景滤镜
        // [129] -> (ByteOffset: +0x10, Bit: 1)
        GameViewportClient->EngineShowFlags[0x10] &= ~(1 << 1);

        // 禁用天空和反射
        // [139] -> (ByteOffset: +0x11, Bit: 3)
        GameViewportClient->EngineShowFlags[0x11] &= ~(1 << 3);

        // 解决水面闪烁
        // [220] -> (ByteOffset: +0x1B, Bit: 4)
        GameViewportClient->EngineShowFlags[0x1B] &= ~(1 << 4);

        // 辅助宏：通过 KismetSystemLibrary 执行虚幻引擎控制台指令
        auto Exec = [WorldContextObject](const wchar_t* Cmd) {
            SDK::UKismetSystemLibrary::ExecuteConsoleCommand(WorldContextObject, SDK::FString(Cmd), nullptr);
            };

        Exec(L"r.TemporalAA.Upsampling 1");     // 开启 TAA 时空抗锯齿超采样 (提高边缘平滑度与画面重建质量)
        Exec(L"r.AntiAliasingMethod 2");        // 将抗锯齿算法切换为 TAA (0 = 关闭, 1 = FXAA, 2 = TAA, 3 = TSR)
        Exec(L"r.SkeletalMeshLODBias 2");       // 骨骼网格体 (如角色、生物模型) 细节等级偏移，强行降低模型面数以提升性能
        Exec(L"r.MaterialQualityLevel 0");      // 将全局材质质量设为最低级 (0 = Low, 1 = Medium, 2 = High)，简化复杂材质计算
        Exec(L"r.SubsurfaceScattering 0");      // 禁用次表面散射 (SSS) 特效，关闭皮肤、树叶等透光与漫射计算
        Exec(L"r.ParticleLODBias 5");           // 粒子系统细节等级偏移，大幅简化或远距离隐藏烟雾、火花等粒子特效
        Exec(L"p.ClothPhysics 0");              // 禁用服装与布料物理模拟 (如斗篷、装备披风)，降低 CPU 物理计算负担
        Exec(L"r.DisableDistortion 1");         // 禁用画面扭曲效果 (如爆炸热浪、水下波纹等折射特效)

        // 1. 阴影
        Exec(L"r.ShadowQuality 0");                     // 关闭通用阴影质量级别 (0 = Off/Lowest)
        Exec(L"r.Shadow.MaxResolution 0");              // 阴影贴图最大分辨率设为 0，禁用阴影纹理生成
        Exec(L"r.Shadow.DistanceScale 0");              // 阴影渲染渲染距离归零，超出 0 距离不再渲染阴影
        Exec(L"r.Shadow.CSM.MaxCascades 0");            // 级联阴影 (CSM) 层级设为 0，关闭级联动态阴影
        Exec(L"r.Shadow.Virtual.Enable 0");             // 禁用 UE5 虚拟阴影贴图 (Virtual Shadow Maps / VSM)
        Exec(L"r.DistanceFieldShadowing 0");            // 禁用网格体距离场阴影 (Distance Field Shadows)
        Exec(L"r.ContactShadows 0");                    // 禁用屏幕空间接触阴影 (Contact Shadows)
        Exec(L"grass.DisableDynamicShadows 1");         // 强制禁用草地与细小植被的动态投射阴影

        // 2. 光照
        Exec(L"r.LightFunctionQuality 0");              // 禁用灯光函数 (Light Function) 材质特效
        Exec(L"r.DynamicGlobalIlluminationMethod 0");   // 关掉水会闪烁
        Exec(L"r.LightShaftQuality 0");                 // 禁用光轴与体积光斑 (丁达尔效应/Light Shafts)

        Exec(L"r.LightMaxDrawDistanceScale 1");         //  不要光源最大绘制距离倍率

        Exec(L"r.SkylightIntensityMultiplier 5");       // 天光
        Exec(L"r.Lumen.ScreenProbeGather.ScreenTraces 0"); // 禁用 Lumen 屏幕空间光线追踪探测
        Exec(L"r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution 16"); // 将 Radiance Cache 探针分辨率降至最低 (16)

        // 3. 反射
        Exec(L"r.ReflectionEnvironment 0");             // 反射环境
        Exec(L"r.SSR.Quality 0");                       // 禁用屏幕空间反射 (Screen Space Reflections)
        Exec(L"r.Lumen.Reflections.Allow 0");           // 禁用 Lumen 动态高精度反射
        Exec(L"r.Lumen.Reflections.Contrast 0");        // 将 Lumen 反射对比度归零，降低计算复杂度
        Exec(L"r.MinRoughnessOverride 1");              // 强制所有材质粗糙度设为最大值 1.0 (消除光滑高光与反射)

        // 4. 植被
        Exec(L"foliage.DensityScale 0");                // 植被生成密度归零 (隐藏大部分渲染植被)
        Exec(L"foliage.LODDistanceScale 0");            // 植被 LOD 距离比例归零 (强制使用最低面数模型)
        Exec(L"grass.DensityScale 0");                  // 草地生成密度归零 (隐藏地表小草)
        Exec(L"grass.SizeScale 0");                     // 草地渲染尺寸归零
        Exec(L"r.Foliage.WPODisableMultiplier 1");      // 禁用植被的世界位置偏移 (WPO / 风吹风动动画)
        Exec(L"r.Foliage.AutoBoundsWPODisableMax 1");   // 自动关闭远距离植被的动画计算
        Exec(L"r.Nanite.MaxPixelsPerEdge 10");          // 极简化 Nanite 几何体 (增加每条边的像素网格大小以降低几何面数)
        Exec(L"wp.Runtime.HLOD.ForceDisable 1");        // 强制禁用世界分区 (World Partition) 远景 HLOD 生成

        // 5. 后处理
        Exec(L"r.BloomQuality 0");                      // 禁用辉光/泛光 (Bloom) 特效
        Exec(L"r.DepthOfFieldQuality 0");               // 禁用景深 (Depth of Field) 模糊效果
        Exec(L"r.MotionBlurQuality 0");                 // 禁用运动模糊 (Motion Blur) 采样质量
        Exec(L"r.MotionBlur.Amount 0");                 // 将运动模糊强度设为 0
        Exec(L"r.AmbientOcclusionLevels 0");            // 关闭环境光遮蔽 (AO / SSAO) 级别
        Exec(L"r.AOOverwriteSceneColor 0");             // 为 1 会导致色彩搞砸
        Exec(L"r.LensFlareQuality 0");                  // 禁用镜头光晕 (Lens Flare) 特效
        Exec(L"fx.EnableNiagaraSpriteRendering 0");     // 禁用 Niagara 粒子系统的 Sprite 纹理渲染
        Exec(L"ark.MaxActiveDestroyedMeshGeoCollectionCount 0"); // 禁用破碎网格体 (Geometry Collection) 物理碎片生成
        Exec(L"r.EyeAdaptationQuality 0");              // 不要禁用曝光采样质量

        // 6. 大气
        Exec(L"r.SkyAtmosphere 0");                     // 天空大气渲染
        Exec(L"r.Fog 0");                               // 禁用通用场景雾效 (Height Fog)
        Exec(L"r.VolumetricFog 0");                     // 禁用体积雾 (Volumetric Fog) 高消耗渲染
        Exec(L"r.VolumetricCloud 0");                   // 禁用 UE5 3D 动态体积云 (Volumetric Cloud)

        // 7. 材质
        Exec(L"r.MipMapLODBias 0");                     // 纹理 MipMap 偏移 (设为 0 保持正常贴图，若需贴图极糊可改大如 3~5)
        Exec(L"r.EmitterSpawnRateScale 0");             // 传统级联粒子 (Cascade Particle) 生成速率归零
        Exec(L"r.ParticleLODBias 10");                  // 强制粒子系统使用最低细节级别 (LOD)
        Exec(L"r.TranslucencyVolumeBlur 0");            // 禁用半透明体积模糊计算
        Exec(L"r.VT.EnableFeedback 0");                 // 禁用虚拟纹理 (Virtual Texture) 采样反馈机制

        // 已经被flags关了
        // 8. 水体系统
        // Exec(L"r.Water.SingleLayer 0");                 // 关闭单层水体 (Single Layer Water) 渲染管线
        // Exec(L"r.Water.SingleLayer.Reflection 0");      // 关闭水面反射计算
    }

    __forceinline bool Welcome(SDK::UWorld* World, SDK::UCanvas* Canvas)
    {
        if (g_Config::bInitWelcome) return true;

        // 只播放一次
        static bool bFinished = false;
        if (bFinished) {
            g_Config::bInitWelcome = true;
            return true;
        }

        if (!World || !Canvas || Canvas->SizeX <= 0 || Canvas->SizeY <= 0)
            return bFinished;

        // 使用游戏世界 DeltaTime 驱动一次性启动动画。
        float dt = SDK::UGameplayStatics::GetWorldDeltaSeconds(World);
        static float CurrentTime = 0.0f;
        CurrentTime += dt;

        // 短促的卡片式节奏：等待 -> 弹入/加载 -> 确认 -> 退出。
        constexpr float DelayDuration = 0.35f;
        constexpr float EnterDuration = 0.45f;
        constexpr float HoldDuration = 1.65f;
        constexpr float ExitDuration = 0.40f;
        constexpr float TotalDuration = DelayDuration + EnterDuration + HoldDuration + ExitDuration;

        // 延迟阶段：静默等待
        if (CurrentTime < DelayDuration)
            return false;

        // 动画整体结束
        if (CurrentTime >= TotalDuration)
        {
            bFinished = true;
            g_Config::bInitWelcome = true;
            return true;
        }

        Shadow::ShadowDrawList* DrawList = Shadow::GetBackgroundDrawList();
        if (!DrawList)
            return false;

        const float ScreenW = static_cast<float>(Canvas->SizeX);
        const float ScreenH = static_cast<float>(Canvas->SizeY);
        const Shadow::Vec2 Center = { ScreenW * 0.5f, ScreenH * 0.5f };

        auto EaseOutCubic = [](float t) -> float {
            t = std::clamp(t, 0.0f, 1.0f);
            const float Inv = 1.0f - t;
            return 1.0f - Inv * Inv * Inv;
            };
        auto EaseInCubic = [](float t) -> float {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * t;
            };

        const float ActiveTime = CurrentTime - DelayDuration;
        const float EnterProgress = std::clamp(ActiveTime / EnterDuration, 0.0f, 1.0f);
        const float ExitProgress = std::clamp(
            (ActiveTime - EnterDuration - HoldDuration) / ExitDuration, 0.0f, 1.0f);
        const float EnterEase = EaseOutCubic(EnterProgress);
        const float ExitEase = EaseInCubic(ExitProgress);
        const float ContentAlpha = std::clamp(EnterEase * (1.0f - ExitEase), 0.0f, 1.0f);

        auto MakeCol = [](float r, float g, float b, float a) -> Shadow::Color {
            return Shadow::Color(
                std::clamp(r, 0.0f, 1.0f),
                std::clamp(g, 0.0f, 1.0f),
                std::clamp(b, 0.0f, 1.0f),
                std::clamp(a, 0.0f, 1.0f)
            );
            };

        // 与菜单一致的高对比纯色：白底、硬黑框、荧光粉和黄绿色。
        const Shadow::Color Overlay = MakeCol(0.0f, 0.0f, 0.0f, ContentAlpha * 0.18f);
        const Shadow::Color Black = MakeCol(0.0f, 0.0f, 0.0f, ContentAlpha);
        const Shadow::Color White = MakeCol(1.0f, 1.0f, 1.0f, ContentAlpha);
        const Shadow::Color Pink = MakeCol(1.0f, 0.0f, 0.431f, ContentAlpha);
        const Shadow::Color Lime = MakeCol(0.8f, 1.0f, 0.0f, ContentAlpha);

        DrawList->AddRectFilled({ 0.0f, 0.0f }, { ScreenW, ScreenH }, Overlay);
        if (ContentAlpha <= 0.005f)
            return false;

        const float UiScale = std::clamp(
            std::min(ScreenW / 1920.0f, ScreenH / 1080.0f), 0.75f, 1.60f);
        const float PanelW = 460.0f * UiScale;
        const float PanelH = 210.0f * UiScale;
        const float ExitOffset = ExitEase * -24.0f * UiScale;
        const float EnterOffset = (1.0f - EnterEase) * 36.0f * UiScale;
        const Shadow::Vec2 PanelPos = {
            Center.x - PanelW * 0.5f,
            Center.y - PanelH * 0.5f + EnterOffset + ExitOffset
        };
        const Shadow::Vec2 PanelSize = { PanelW, PanelH };
        const float ShadowOffset = 10.0f * UiScale;
        const float Border = std::max(3.0f, 4.0f * UiScale);
        const float HeaderH = 30.0f * UiScale;

        // 硬边卡片与右下偏移阴影。
        DrawList->AddRectFilled(
            { PanelPos.x + ShadowOffset, PanelPos.y + ShadowOffset }, PanelSize, Black);
        DrawList->AddRectFilled(PanelPos, PanelSize, White);

        const float HeaderReveal = EaseOutCubic(std::clamp(EnterProgress * 1.45f, 0.0f, 1.0f));
        if (HeaderReveal > 0.001f) {
            DrawList->AddRectFilled(PanelPos, { PanelW * HeaderReveal, HeaderH }, Pink);
        }
        DrawList->AddLine(
            { PanelPos.x, PanelPos.y + HeaderH },
            { PanelPos.x + PanelW, PanelPos.y + HeaderH }, Black, Border * 0.75f);
        DrawList->AddRect(PanelPos, PanelSize, Black, Border);

#if defined(__cpp_char8_t)
        const char* BrandText = reinterpret_cast<const char*>(u8"38s自用");
        const char* LoadingText = reinterpret_cast<const char*>(u8"正在初始化界面");
        const char* ReadyText = reinterpret_cast<const char*>(u8"界面已就绪");
#else
        const char* BrandText = u8"38s自用";
        const char* LoadingText = u8"正在初始化界面";
        const char* ReadyText = u8"界面已就绪";
#endif

        SDK::UFont* Font = Shadow::DefaultFont;
        auto MeasureText = [&](const char* Text, float FontScale) -> Shadow::Vec2 {
            if (!Font) return { 0.0f, 0.0f };
            Shadow::PushFont(Font, FontScale);
            const Shadow::Vec2 Size = Shadow::MeasureTextSize(Text);
            Shadow::PopFont();
            return Size;
            };
        auto DrawFlatText = [&](const char* Text, float FontScale,
            Shadow::Vec2 Pos, Shadow::Color Color) {
                if (!Font) return;
                const Shadow::Color Transparent = MakeCol(0.0f, 0.0f, 0.0f, 0.0f);
                DrawList->AddText(Font, FontScale, Transparent, Transparent, Pos, Color, Text);
            };

        const float TextReveal = std::clamp((EnterProgress - 0.10f) / 0.45f, 0.0f, 1.0f);
        const Shadow::Color TextBlack = MakeCol(0.0f, 0.0f, 0.0f, ContentAlpha * TextReveal);
        const Shadow::Color TextMuted = MakeCol(0.235f, 0.235f, 0.235f, ContentAlpha * TextReveal);
        const float TitleScale = 1.12f * UiScale;
        const float StatusScale = 0.72f * UiScale;
        const Shadow::Vec2 TitleSize = MeasureText(BrandText, TitleScale);
        DrawFlatText(BrandText, TitleScale,
            { PanelPos.x + 28.0f * UiScale,
              PanelPos.y + 53.0f * UiScale - TitleSize.y * 0.08f },
            TextBlack);

        constexpr int SegmentCount = 9;
        const float ProgressDuration = EnterDuration + HoldDuration * 0.75f;
        const float Progress = std::clamp(
            (ActiveTime - EnterDuration * 0.25f) / ProgressDuration, 0.0f, 1.0f);
        const char* StatusText = Progress >= 0.999f ? ReadyText : LoadingText;
        DrawFlatText(StatusText, StatusScale,
            { PanelPos.x + 29.0f * UiScale, PanelPos.y + 105.0f * UiScale },
            TextMuted);

        // 分段进度条逐格跳变，避免柔和渐变，与菜单的纯色块一致。
        const float BarX = PanelPos.x + 28.0f * UiScale;
        const float BarY = PanelPos.y + 154.0f * UiScale;
        const float BarW = PanelW - 56.0f * UiScale;
        const float BarH = 22.0f * UiScale;
        const float Gap = 5.0f * UiScale;
        const float SegmentW = (BarW - Gap * (SegmentCount - 1)) / SegmentCount;
        const int FilledSegments = std::min(
            SegmentCount, static_cast<int>(std::floor(Progress * SegmentCount + 0.001f)));

        for (int i = 0; i < SegmentCount; ++i) {
            const Shadow::Vec2 SegmentPos = {
                BarX + i * (SegmentW + Gap), BarY
            };
            const Shadow::Vec2 SegmentSize = { SegmentW, BarH };
            DrawList->AddRectFilled(SegmentPos, SegmentSize,
                i < FilledSegments ? Lime : White);
            DrawList->AddRect(SegmentPos, SegmentSize, Black,
                std::max(1.5f, 2.0f * UiScale));
        }

        // 最后一格完成时弹出确认块。
        const float ReadyProgress = std::clamp((Progress - 0.90f) / 0.10f, 0.0f, 1.0f);
        if (ReadyProgress > 0.001f) {
            const float ReadyAlpha = ContentAlpha * EaseOutCubic(ReadyProgress);
            const Shadow::Color ReadyBlack = MakeCol(0.0f, 0.0f, 0.0f, ReadyAlpha);
            const Shadow::Color ReadyLime = MakeCol(0.8f, 1.0f, 0.0f, ReadyAlpha);
            const float BoxSize = 38.0f * UiScale;
            const float BoxRise = (1.0f - EaseOutCubic(ReadyProgress)) * 14.0f * UiScale;
            const Shadow::Vec2 BoxPos = {
                PanelPos.x + PanelW - 66.0f * UiScale,
                PanelPos.y + 55.0f * UiScale + BoxRise
            };
            DrawList->AddRectFilled(
                { BoxPos.x + 5.0f * UiScale, BoxPos.y + 5.0f * UiScale },
                { BoxSize, BoxSize }, ReadyBlack);
            DrawList->AddRectFilled(BoxPos, { BoxSize, BoxSize }, ReadyLime);
            DrawList->AddRect(BoxPos, { BoxSize, BoxSize }, ReadyBlack,
                std::max(2.0f, 3.0f * UiScale));
            DrawList->AddLine(
                { BoxPos.x + 9.0f * UiScale, BoxPos.y + 20.0f * UiScale },
                { BoxPos.x + 16.0f * UiScale, BoxPos.y + 28.0f * UiScale },
                ReadyBlack, std::max(2.0f, 3.0f * UiScale));
            DrawList->AddLine(
                { BoxPos.x + 16.0f * UiScale, BoxPos.y + 28.0f * UiScale },
                { BoxPos.x + 30.0f * UiScale, BoxPos.y + 11.0f * UiScale },
                ReadyBlack, std::max(2.0f, 3.0f * UiScale));
        }

        return false;
    }

    __forceinline SDK::AShooterGameState* GetAShooterGameState() {
        SDK::AShooterGameState* GS = nullptr;
        SDK::UWorld* World = SDK::UWorld::GetWorld();

        if (World && World->GameState && World->GameState->IsA(SDK::AShooterGameState::StaticClass())) {
            GS = static_cast<SDK::AShooterGameState*>(World->GameState);
        }

        return GS;
    }

    __forceinline float GetClientPing() {
        float ping = 0.f;

        SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
        if (LocalPC && LocalPC->PlayerState) {
            ping = LocalPC->PlayerState->GetExactPing();
        }

        return ping;
    }

    __forceinline SDK::UFont* GetOpenSansRegular12() {
        static SDK::UFont* OpenSansRegular12 = nullptr;

        if (!OpenSansRegular12) {
            SDK::UObject* _Font = SDK::UObject::FindObject("Font OpenSansRegular12.OpenSansRegular12");
            if (_Font && _Font->IsA(SDK::UFont::StaticClass())) OpenSansRegular12 = (SDK::UFont*)_Font;
        }

        return OpenSansRegular12;
    }

    __forceinline SDK::UFont* GetSansationBold18() {
        static SDK::UFont* SansationBold18 = nullptr;

        if (!SansationBold18) {
            SDK::UObject* _Font = SDK::UObject::FindObject("Font SansationBold18.SansationBold18");
            if (_Font && _Font->IsA(SDK::UFont::StaticClass())) SansationBold18 = (SDK::UFont*)_Font;
        }

        return SansationBold18;
    }

    __forceinline float GetFPS() {
        static float time_accumulator = 0.f; // 累积时间 (秒)
        static int   frame_count = 0;        // 累积渲染帧数
        static float last_fps = 0.f;         // 上一次计算并缓存的 FPS

        float dt = Shadow::GetIO().DeltaTime;

        // 累加时间和帧数
        time_accumulator += dt;
        frame_count++;

        // 达到 500ms (0.5秒) 更新一次
        if (time_accumulator >= 0.5f) {
            last_fps = (time_accumulator > 0.f) ? (static_cast<float>(frame_count) / time_accumulator) : 0.f;

            // 重置计数器
            time_accumulator = 0.f;
            frame_count = 0;
        }

        return last_fps;
    }

    __forceinline void DropOrUseItem() {
        SDK::AShooterPlayerController* LocalPC = static_cast<SDK::AShooterPlayerController*>(g_Util::GetLocalPC());
        if (LocalPC && LocalPC->Pawn && LocalPC->Pawn->IsA(SDK::APrimalCharacter::StaticClass())) {
            SDK::APrimalCharacter* LocalCharacter = static_cast<SDK::APrimalCharacter*>(LocalPC->Pawn);

            if (LocalCharacter) {
                SDK::UPrimalInventoryComponent* LocalInventory = LocalCharacter->MyInventoryComponent;

                if (LocalInventory) {
                    // 执行丢弃逻辑
                    if (g_Config::bDropItem) {
                        LocalPC->ServerDropFromRemoteInventory(LocalInventory, g_Config::dropItemID);
                        // 重置状态
                        g_Config::bDropItem = false;
                    }

                    // 执行使用逻辑
                    if (g_Config::bUseItem) {
                        // 执行操作
                        LocalPC->ServerRequestInventoryUseItem(LocalInventory, g_Config::useItemID, g_Config::useItemSlotID);

                        // 重置状态
                        g_Config::bUseItem = false;
                        // SDK::FItemNetID 通常是结构体，不需要像字符串一样手动清理，下次会被覆盖
                    }
                }
            }
        }
    }

    __forceinline void ADD_MH(std::string pattern, bool* hookOK, void* hk, void* o) {
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, hk, reinterpret_cast<LPVOID*>(o)) == MH_OK) {
                MH_EnableHook(targetAddr);
                *hookOK = true;
            }
        }
    }

    __forceinline void ADD_MH(void* targetAddr, bool* hookOK, void* hk, void* o) {
        if (targetAddr) {
            if (MH_CreateHook(targetAddr, hk, reinterpret_cast<LPVOID*>(o)) == MH_OK) {
                MH_EnableHook(targetAddr);
                *hookOK = true;
            }
        }
    }

    static __forceinline bool HasCommandLineArg(std::wstring_view targetArg) noexcept {
        struct CmdCache {
            int argc = 0;
            LPWSTR* argv = nullptr;

            CmdCache() noexcept {
                argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            }
        };

        static const CmdCache cache;

        if (cache.argv == nullptr) {
            return false;
        }

        const size_t targetLen = targetArg.size();

        for (int i = 0; i < cache.argc; ++i) {
            const wchar_t* currentArg = cache.argv[i];
            if (currentArg == nullptr) continue;

            if (wcslen(currentArg) != targetLen) continue;

            bool isMatch = true;
            for (size_t j = 0; j < targetLen; ++j) {
                if (towlower(targetArg[j]) != towlower(currentArg[j])) {
                    isMatch = false;
                    break;
                }
            }

            if (isMatch) {
                return true;
            }
        }

        return false;
    }
}
