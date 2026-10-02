#pragma once
#include "../Config/Configs.h"
#include "../Config/InterfaceRules.h"
#include "../Util/Util.h"
#include "../Log/LogManager.h"
#include "../ESP/DrawESP.h"
#include "Hack.h"
#include "FeatureExpansionRules.h"
#include "NativeMountRules.h"
#include "DeinoAirLatchRules.h"
#include "DeinoGlideProbeRules.h"
#include "DeinoActionRules.h"
#include "DeinoFlightRules.h"
#include "DeinoFlightPreview.h"
#include "DeinoFlightUI.h"
#include "AutoCraftScheduler.h"
#include "GachaOpenScheduler.h"
#include "FireRateRules.h"
#include "InstantReloadRules.h"
#include "InstantReloadPatch.h"
#include "BuffSoupRules.h"
#include "FireExtinguishRules.h"
#include "TekHeatRules.h"
#include "AutoLootRules.h"
#include "CameraControls.h"
#include "WorldUtilityRules.h"
#include "NoteExperiment.h"
#include "InventoryTransferRules.h"
#include "BackpackRules.h"
#include "TekSpear.h"
#include "../Lua/SessionSnapshot.h"
#include "../Menu/ShortcutRuntime.h"
#include "../Language/LanguageManager.h"
#include "HarvestRules.h"
#include "../Legit/Aimbot/Aimbot.h"
#include "../../external/CppSDK/SDK/ShooterGame_parameters.hpp"
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../../external/ezVMT/include/ezVMT.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <locale>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace g_Hack {
    template<class... Args> std::string FormatLocalized(const char* Source, Args&&... Values) {
        return std::vformat(LanguageManager::Get().TranslateCopy(Source), std::make_format_args(Values...));
    }

    // 秒换弹／弓秒蓄力的 tick 在末尾包含的 Hack/InstantReload.inl 中定义。
    // 这里只能前向声明匿名命名空间内的定义；若声明在 g_Hack 命名空间会得到
    // 内部链接定义 + 外部链接声明，进而链接失败。
    namespace {
        void InstantReloadTick(SDK::UWorld* World);
        void InstantChargeTick(SDK::UWorld* World);
    }

    void DumpServerInfo() {
        SDK::UWorld* World = SDK::UWorld::GetWorld();
        if (!World || !World->NetDriver || !World->NetDriver->ServerConnection) {
            g_LogManager::AddLog(255, 50, 55, 255, "当前不在服务器内");
            return;
        }

        SDK::UNetConnection* ServerConnection = World->NetDriver->ServerConnection;

        g_LogManager::AddLog(255, 200, 255, 255, std::format("[{}] [{}]", ServerConnection->GetFirstIP(), ServerConnection->GetPort()));
    }

    void Suicide(SDK::UWorld* World) {
        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController) return;

        if (!LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) return;
        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);

        if (!PC || !PC->Character) return;

        SDK::AShooterCharacter* TargetHuman = nullptr;

        // 情况 1：当前控制的是人类本身
        if (PC->Character->IsA(SDK::AShooterCharacter::StaticClass()) && !PC->Character->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            TargetHuman = static_cast<SDK::AShooterCharacter*>(PC->Character);
        }
        // 情况 2：当前控制的是恐龙，从 Rider 弱指针中提取人类
        else if (PC->Character->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            SDK::APrimalDinoCharacter* Dino = static_cast<SDK::APrimalDinoCharacter*>(PC->Character);

            // 检查弱指针是否有效，并转换为强指针（裸指针）
            if (Dino) {
                TargetHuman = Dino->Rider.Get();
            }
        }

        // 统一执行自杀逻辑
        if (TargetHuman) {
            TargetHuman->BPSuicide();
        }
    }

    namespace {
        std::string NormalizeGachaItemName(std::string value) {
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return !std::isalnum(ch);
                }), value.end());

            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
                });
            return value;
        }

        bool IsGachaCrystal(const SDK::UPrimalItem* Item) {
            if (!Item || Item->ItemQuantity <= 0 || Item->bIsBlueprint) return false;

            // The UObject name is language-independent and normally contains
            // "PrimalItemResource_GachaCrystal". Normalize separators so minor
            // asset-name variations such as Gacha_Crystal are also supported.
            const std::string objectName = NormalizeGachaItemName(Item->GetName());
            if (objectName.find("gachacrystal") != std::string::npos) return true;

            if (!Item->DescriptiveNameBase) return false;
            const std::string displayName = Item->DescriptiveNameBase.ToString();
            if (displayName.find("嘎查水晶") != std::string::npos) return true;

            const std::string normalizedDisplayName = NormalizeGachaItemName(displayName);
            return normalizedDisplayName.find("gachacrystal") != std::string::npos;
        }

        SDK::APrimalCharacter* GetPlayerInventoryOwner(SDK::AShooterPlayerController* PC) {
            if (!PC) return nullptr;

            if (PC->Character && PC->Character->IsA(SDK::AShooterCharacter::StaticClass()) &&
                !PC->Character->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return static_cast<SDK::APrimalCharacter*>(PC->Character);
            }

            // The generated TWeakObjectPtr::Get() helper ignores the weak
            // serial number. Do not follow Dino->Rider here: Ferox form
            // replacement can reuse that object slot and turn it into an
            // unrelated UObject. PC->Character is the authoritative human
            // reference while mounted; the pawn fallback is only valid when
            // the pawn itself is a human shooter character.
            if (PC->Pawn && PC->Pawn->IsA(SDK::AShooterCharacter::StaticClass()) &&
                !PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return static_cast<SDK::APrimalCharacter*>(PC->Pawn);
            }

            return nullptr;
        }

        bool EqualsNameIgnoreCase(const std::string& Left, const char* Right) {
            if (!Right || Left.size() != std::strlen(Right)) return false;

            for (size_t i = 0; i < Left.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(Left[i])) !=
                    std::tolower(static_cast<unsigned char>(Right[i]))) {
                    return false;
                }
            }
            return true;
        }

        bool IsUsableObject(const SDK::UObject* Object) {
            if (!Object || !Object->Class || Object->IsDefaultObject()) return false;
            if (Object->Flags & SDK::EObjectFlags::BeginDestroyed) return false;
            if (Object->Flags & SDK::EObjectFlags::FinishDestroyed) return false;
            if (Object->Flags & SDK::EObjectFlags::MirroredGarbage) return false;
            return true;
        }

        bool TryGetLiveObjectSerial(const SDK::UObject* ExpectedObject,
            int32_t ObjectIndex, int32_t* OutSerialNumber) {
            if (!ExpectedObject || !OutSerialNumber) return false;

            SDK::TUObjectArray* ObjectArray = SDK::UObject::GObjects.GetTypedPtr();
            if (!ObjectArray || ObjectIndex < 0 ||
                ObjectIndex >= ObjectArray->NumElements) {
                return false;
            }

            const int32_t ChunkIndex =
                ObjectIndex / SDK::TUObjectArray::ElementsPerChunk;
            const int32_t IndexWithinChunk =
                ObjectIndex % SDK::TUObjectArray::ElementsPerChunk;
            if (ChunkIndex < 0 || ChunkIndex >= ObjectArray->NumChunks) return false;

            SDK::FUObjectItem** ObjectChunks = ObjectArray->GetDecrytedObjPtr();
            if (!ObjectChunks || !ObjectChunks[ChunkIndex]) return false;
            const SDK::FUObjectItem* Item =
                &ObjectChunks[ChunkIndex][IndexWithinChunk];
            if (Item->Object != ExpectedObject) return false;

            // ASA's verified 0x18-byte FUObjectItem keeps its serial at 0x10.
            // Compare the registry entry before touching a cached UObject so a
            // destroyed mount can never be dereferenced through a stale pointer.
            std::memcpy(OutSerialNumber,
                reinterpret_cast<const uint8_t*>(Item) + 0x10,
                sizeof(*OutSerialNumber));
            return true;
        }

        template <typename TObject>
        TObject* ResolveCachedObject(TObject* CachedObject, int32_t ObjectIndex) {
            if (!CachedObject || ObjectIndex < 0 || ObjectIndex >= SDK::UObject::GObjects->Num()) return nullptr;
            SDK::UObject* CurrentObject = SDK::UObject::GObjects->GetByIndex(ObjectIndex);
            if (CurrentObject != CachedObject || !IsUsableObject(CurrentObject) ||
                CurrentObject->Index != ObjectIndex) {
                return nullptr;
            }
            return static_cast<TObject*>(CurrentObject);
        }

        SDK::UObject* ResolveObjectAtIndex(int32_t ObjectIndex) {
            if (ObjectIndex < 0 || ObjectIndex >= SDK::UObject::GObjects->Num()) return nullptr;
            SDK::UObject* Object = SDK::UObject::GObjects->GetByIndex(ObjectIndex);
            if (!IsUsableObject(Object) || Object->Index != ObjectIndex) return nullptr;
            return Object;
        }

        SDK::FProperty* FindReflectedProperty(const SDK::UStruct* Type, const char* PropertyName) {
            for (const SDK::UStruct* Current = Type; Current; Current = Current->SuperStruct) {
                for (SDK::FField* Field = Current->ChildProperties; Field; Field = Field->Next) {
                    if (EqualsNameIgnoreCase(Field->Name.ToString(), PropertyName)) {
                        return static_cast<SDK::FProperty*>(Field);
                    }
                }
            }
            return nullptr;
        }

        SDK::FProperty* FindFirstReflectedProperty(const SDK::UStruct* Type,
            std::initializer_list<const char*> PropertyNames) {
            for (const char* PropertyName : PropertyNames) {
                if (SDK::FProperty* Property = FindReflectedProperty(Type, PropertyName)) {
                    return Property;
                }
            }
            return nullptr;
        }

        SDK::UFunction* FindReflectedFunction(const SDK::UClass* Type, const char* FunctionName) {
            for (const SDK::UStruct* Current = Type; Current; Current = Current->SuperStruct) {
                for (SDK::UField* Field = Current->Children; Field; Field = Field->Next) {
                    if (Field->HasTypeFlag(SDK::EClassCastFlags::Function) &&
                        EqualsNameIgnoreCase(Field->GetName(), FunctionName)) {
                        return static_cast<SDK::UFunction*>(Field);
                    }
                }
            }
            return nullptr;
        }

        bool IsWidgetInViewport(SDK::UWidget* Widget) {
            if (!Widget || !Widget->Class) return false;

            SDK::UFunction* Function = FindReflectedFunction(Widget->Class, "IsInViewport");
            if (!Function) return false;

            struct IsInViewportParameters {
                bool ReturnValue;
            } Parameters{};

            const auto OriginalFlags = Function->FunctionFlags;
            Function->FunctionFlags |= SDK::EFunctionFlags::Native;
            Widget->ProcessEvent(Function, &Parameters);
            Function->FunctionFlags = OriginalFlags;
            return Parameters.ReturnValue;
        }

        bool IsWidgetRendered(SDK::UWidget* Widget) {
            if (!Widget || !Widget->Class) return false;

            SDK::UFunction* Function = FindReflectedFunction(Widget->Class, "IsRendered");
            if (!Function) return false;

            struct IsRenderedParameters {
                bool ReturnValue;
            } Parameters{};

            const auto OriginalFlags = Function->FunctionFlags;
            Function->FunctionFlags |= SDK::EFunctionFlags::Native;
            Widget->ProcessEvent(Function, &Parameters);
            Function->FunctionFlags = OriginalFlags;
            return Parameters.ReturnValue;
        }

        bool IsFishingQTEKey(const SDK::FKey& Key);
        bool TryMakeFishingQTEKey(const std::string& Text, SDK::FKey* OutKey);
        bool TryReadFishingQTEKey(SDK::UObject* Object, SDK::FKey* OutKey, int32_t* OutKeyIndex);

        std::string g_FishingQTEStatus = "等待钓鱼 QTE";
        std::string g_AutoEatDrinkStatus = "自动吃喝未启用";
        std::string g_AutoBrewStatus = "自动药酒未启用";
        std::mutex g_AutoPickupStatusMutex;
        std::string g_AutoPickupStatus = "自动拾取未启用";
        std::mutex g_AutoRepairStatusMutex;
        std::string g_AutoRepairStatus = "智能自动修理未启用";
        std::string g_FishingRodMaintenanceStatus = "鱼竿维护未启用";

        void SetAutoPickupStatus(std::string Status) {
            std::scoped_lock Lock(g_AutoPickupStatusMutex);
            g_AutoPickupStatus = std::move(Status);
        }

        void SetAutoRepairStatus(std::string Status) {
            std::scoped_lock Lock(g_AutoRepairStatusMutex);
            g_AutoRepairStatus = std::move(Status);
        }

        bool SameItemNetID(const SDK::FItemNetID& Left, const SDK::FItemNetID& Right) {
            return Left.ItemID1 == Right.ItemID1 && Left.ItemID2 == Right.ItemID2;
        }

        bool IsFishingRodItem(const SDK::UPrimalItem* Item) {
            if (!IsUsableObject(Item) || !Item->Class) return false;
            std::string Identity = Item->Class->GetName();
            Identity += Item->GetName();
            Identity += Item->GetFullName();
            Identity = NormalizeGachaItemName(std::move(Identity));
            return Identity.find("primalitemweaponfishingrod") != std::string::npos ||
                Identity.find("weaponfishingrod") != std::string::npos;
        }

        float GetConsumableRestoreAmount(const SDK::UPrimalItem* Item,
            SDK::EPrimalCharacterStatusValue TargetValue) {
            if (!IsUsableObject(Item)) return 0.0f;

            float RestoreAmount = 0.0f;
            for (int32_t Index = 0; Index < Item->UseItemAddCharacterStatusValues.Num(); ++Index) {
                const SDK::FUseItemAddCharacterStatusValue& Effect =
                    Item->UseItemAddCharacterStatusValues[Index];
                if (Effect.StatusValueType != TargetValue) continue;
                RestoreAmount += std::max(0.0f, Effect.BaseAmountToAdd);
                RestoreAmount += std::max(0.0f, Effect.PercentAbsoluteMaxValue) * 100.0f;
            }
            return RestoreAmount;
        }

        bool HasUnsafeAutoConsumeEffect(const SDK::UPrimalItem* Item) {
            if (!IsUsableObject(Item)) return true;
            for (int32_t Index = 0; Index < Item->UseItemAddCharacterStatusValues.Num(); ++Index) {
                const SDK::FUseItemAddCharacterStatusValue& Effect =
                    Item->UseItemAddCharacterStatusValues[Index];
                const float Amount = Effect.BaseAmountToAdd;
                const bool HasNegativePercent = Effect.PercentAbsoluteMaxValue < 0.0f ||
                    Effect.PercentAbsoluteMinValue < 0.0f;
                if ((Effect.StatusValueType == SDK::EPrimalCharacterStatusValue::Health ||
                    Effect.StatusValueType == SDK::EPrimalCharacterStatusValue::Stamina ||
                    Effect.StatusValueType == SDK::EPrimalCharacterStatusValue::Oxygen ||
                    Effect.StatusValueType == SDK::EPrimalCharacterStatusValue::Food ||
                    Effect.StatusValueType == SDK::EPrimalCharacterStatusValue::Water) &&
                    (Amount < 0.0f || HasNegativePercent)) {
                    return true;
                }
                if (Effect.StatusValueType == SDK::EPrimalCharacterStatusValue::Torpidity &&
                    (Amount > 0.0f || Effect.PercentAbsoluteMaxValue > 0.0f ||
                        Effect.PercentAbsoluteMinValue > 0.0f)) {
                    return true;
                }
            }
            return false;
        }

        bool IsSafeAutoConsumable(const SDK::UPrimalItem* Item,
            SDK::EPrimalCharacterStatusValue TargetValue) {
            if (!IsUsableObject(Item) || !Item->Class || Item->ItemQuantity <= 0 ||
                Item->bIsBlueprint || Item->bIsEngram || Item->bIsItemSkin ||
                HasUnsafeAutoConsumeEffect(Item) ||
                GetConsumableRestoreAmount(Item, TargetValue) <= 0.0f) {
                return false;
            }

            std::string Identity = Item->Class->GetName();
            Identity += Item->GetName();
            Identity = NormalizeGachaItemName(std::move(Identity));

            if (TargetValue == SDK::EPrimalCharacterStatusValue::Water) {
                if (Item->MyConsumableType != SDK::EPrimalConsumableType::Water) return false;
                const bool IsWaterContainer = Identity.find("waterskin") != std::string::npos ||
                    Identity.find("waterjar") != std::string::npos ||
                    Identity.find("canteen") != std::string::npos ||
                    Identity.find("watercontainer") != std::string::npos;
                if (!IsWaterContainer) return false;
                // Water containers use durability as their remaining water level.
                return !Item->bUseItemDurability || Item->ItemDurability > Item->MinItemDurability + 0.01f;
            }

            if (TargetValue != SDK::EPrimalCharacterStatusValue::Food ||
                Item->MyConsumableType != SDK::EPrimalConsumableType::Food) {
                return false;
            }

            const bool IsSafeBerry = Identity.find("amarberry") != std::string::npos ||
                Identity.find("azulberry") != std::string::npos ||
                Identity.find("tintoberry") != std::string::npos ||
                Identity.find("mejoberry") != std::string::npos;
            const bool IsPreparedMeat = Identity.find("cookedmeat") != std::string::npos ||
                Identity.find("cookedfish") != std::string::npos ||
                Identity.find("cookedprime") != std::string::npos ||
                Identity.find("cookedlamb") != std::string::npos ||
                Identity.find("jerky") != std::string::npos;
            const bool IsVegetable = Identity.find("veggie") != std::string::npos ||
                Identity.find("citronal") != std::string::npos ||
                Identity.find("longrass") != std::string::npos ||
                Identity.find("rockarrot") != std::string::npos ||
                Identity.find("savoroot") != std::string::npos;
            return IsSafeBerry || IsPreparedMeat || IsVegetable;
        }

        SDK::UPrimalItem* FindBestAutoConsumable(SDK::UPrimalInventoryComponent* Inventory,
            SDK::EPrimalCharacterStatusValue TargetValue) {
            if (!Inventory) return nullptr;

            SDK::UPrimalItem* BestItem = nullptr;
            float BestScore = -1.0f;
            auto Consider = [&](const SDK::TArray<SDK::UPrimalItem*>& Items) {
                for (int32_t Index = 0; Index < Items.Num(); ++Index) {
                    SDK::UPrimalItem* Item = Items[Index];
                    if (!IsSafeAutoConsumable(Item, TargetValue)) continue;

                    const float Score = GetConsumableRestoreAmount(Item, TargetValue);
                    if (Score > BestScore) {
                        BestScore = Score;
                        BestItem = Item;
                    }
                }
            };
            Consider(Inventory->InventoryItems);
            // Include the survivor's quick bar as well as regular inventory.
            Consider(Inventory->ItemSlots);
            return BestItem;
        }

        bool IsRequestedBrew(const SDK::UPrimalItem* Item,
            SDK::EPrimalCharacterStatusValue TargetValue) {
            if (!IsUsableObject(Item) || !Item->Class || Item->ItemQuantity <= 0 ||
                Item->bIsBlueprint || Item->bIsEngram || Item->bIsItemSkin ||
                HasUnsafeAutoConsumeEffect(Item)) {
                return false;
            }

            std::string Identity = Item->Class->GetName();
            Identity += Item->GetName();
            Identity += Item->GetFullName();
            Identity = NormalizeGachaItemName(std::move(Identity));

            const bool NameMatches = TargetValue == SDK::EPrimalCharacterStatusValue::Health ?
                (Identity.find("healsoup") != std::string::npos ||
                    Identity.find("medicalbrew") != std::string::npos) :
                (Identity.find("staminasoup") != std::string::npos ||
                    Identity.find("energybrew") != std::string::npos);
            if (!NameMatches) return false;

            // The stock brews expose their recovery in this array. Some modded
            // variants apply it through Blueprint instead, so an exact asset-name
            // match remains sufficient when the reflected array is empty.
            const float RestoreAmount = GetConsumableRestoreAmount(Item, TargetValue);
            return RestoreAmount > 0.0f || Item->UseItemAddCharacterStatusValues.Num() == 0;
        }

        SDK::UPrimalItem* FindRequestedBrew(SDK::UPrimalInventoryComponent* Inventory,
            SDK::EPrimalCharacterStatusValue TargetValue) {
            if (!Inventory) return nullptr;
            for (SDK::UPrimalItem* Item : Inventory->InventoryItems)
                if (IsRequestedBrew(Item, TargetValue)) return Item;
            for (SDK::UPrimalItem* Item : Inventory->ItemSlots)
                if (IsRequestedBrew(Item, TargetValue)) return Item;
            return nullptr;
        }

        SDK::UPrimalItem* FindBestReplacementFishingRod(
            SDK::UPrimalInventoryComponent* Inventory, const SDK::FItemNetID* ExcludedItemID) {
            if (!Inventory) return nullptr;

            SDK::UPrimalItem* BestItem = nullptr;
            for (int32_t Index = 0; Index < Inventory->InventoryItems.Num(); ++Index) {
                SDK::UPrimalItem* Item = Inventory->InventoryItems[Index];
                if (!IsFishingRodItem(Item) || Item->bIsBlueprint || Item->bIsEngram ||
                    (ExcludedItemID && SameItemNetID(Item->ItemID, *ExcludedItemID)) ||
                    (Item->bUseItemDurability && Item->ItemDurability <= 0.01f)) {
                    continue;
                }

                if (!BestItem ||
                    (Item->WeaponClipAmmo > 0 && BestItem->WeaponClipAmmo <= 0) ||
                    ((Item->WeaponClipAmmo > 0) == (BestItem->WeaponClipAmmo > 0) &&
                        Item->ItemDurability > BestItem->ItemDurability)) {
                    BestItem = Item;
                }
            }
            return BestItem;
        }

        bool IsFishingRelatedObject(const SDK::UObject* Object) {
            if (!IsUsableObject(Object) || !Object->Class) return false;

            std::string Identity = Object->Class->GetName();
            Identity += Object->GetName();
            Identity += Object->GetFullName();
            Identity = NormalizeGachaItemName(std::move(Identity));
            return Identity.find("weapfishingrod") != std::string::npos ||
                Identity.find("fishingrod") != std::string::npos ||
                Identity.find("reelinggame") != std::string::npos ||
                Identity.find("fishingqte") != std::string::npos ||
                (Identity.find("fishing") != std::string::npos &&
                    (Identity.find("widget") != std::string::npos ||
                        Identity.find("prompt") != std::string::npos ||
                        Identity.find("hud") != std::string::npos));
        }

        SDK::UObject* FindActiveFishingPrompt(bool ScanObjects,
            SDK::FKey* OutKey, int32_t* OutKeyIndex) {
            static int32_t CachedObjectIndex = -1;
            static int32_t ScanCursor = 0;
            static std::vector<int32_t> FishingWidgetIndices;
            if (!OutKey || !OutKeyIndex || !SDK::UObject::GObjects) return nullptr;
            *OutKey = SDK::FKey{};
            *OutKeyIndex = -1;

            auto GetActivePromptAtIndex = [&](int32_t ObjectIndex,
                bool RememberWidget) -> SDK::UObject* {
                if (ObjectIndex < 0 || ObjectIndex >= SDK::UObject::GObjects->Num()) return nullptr;

                SDK::UObject* Object = SDK::UObject::GObjects->GetByIndex(ObjectIndex);
                if (!IsUsableObject(Object) || Object->Index != ObjectIndex) return nullptr;

                SDK::UClass* WidgetClass = SDK::UWidget::StaticClass();
                if (!WidgetClass || !Object->IsA(WidgetClass)) return nullptr;
                if (!IsFishingRelatedObject(Object)) return nullptr;
                if (RememberWidget && FishingWidgetIndices.size() < 128 &&
                    std::find(FishingWidgetIndices.begin(), FishingWidgetIndices.end(),
                        ObjectIndex) == FishingWidgetIndices.end()) {
                    FishingWidgetIndices.push_back(ObjectIndex);
                }

                SDK::UWidget* Widget = static_cast<SDK::UWidget*>(Object);
                const bool IsActive = IsWidgetInViewport(Widget) ||
                    IsWidgetRendered(Widget);
                if (!IsActive) return nullptr;

                SDK::FKey PromptKey{};
                int32_t PromptIndex = -1;
                if (TryReadFishingQTEKey(Widget, &PromptKey, &PromptIndex)) {
                    *OutKey = PromptKey;
                    *OutKeyIndex = PromptIndex;
                    return Object;
                }

                // ASA often renders the prompt as a child TextBlock while the
                // actual key lives on WeapFishingRod_C. Child widgets are not
                // reported by IsInViewport, so use IsRendered/IsVisible and the
                // complete outer path to identify the fishing widget tree.
                SDK::UClass* TextBlockClass = SDK::UTextBlock::StaticClass();
                if (TextBlockClass && Object->IsA(TextBlockClass)) {
                    SDK::UTextBlock* TextBlock = static_cast<SDK::UTextBlock*>(Object);
                    if (TryMakeFishingQTEKey(TextBlock->Text.ToString(), &PromptKey)) {
                        *OutKey = PromptKey;
                        *OutKeyIndex = -1;
                        return Object;
                    }
                }
                return nullptr;
            };

            if (SDK::UObject* CachedPrompt = GetActivePromptAtIndex(CachedObjectIndex, false)) {
                return CachedPrompt;
            }

            CachedObjectIndex = -1;
            for (size_t i = 0; i < FishingWidgetIndices.size();) {
                const int32_t Index = FishingWidgetIndices[i];
                SDK::UObject* Candidate = ResolveObjectAtIndex(Index);
                if (!Candidate || !Candidate->IsA(SDK::UWidget::StaticClass()) ||
                    !IsFishingRelatedObject(Candidate)) {
                    FishingWidgetIndices[i] = FishingWidgetIndices.back();
                    FishingWidgetIndices.pop_back();
                    continue;
                }
                if (SDK::UObject* Prompt = GetActivePromptAtIndex(Index, false)) {
                    CachedObjectIndex = Index;
                    return Prompt;
                }
                ++i;
            }
            if (!ScanObjects) return nullptr;

            // A full UObject walk on the game thread caused a frame spike every
            // 100 ms while this option was enabled. Spread discovery across
            // ticks; once found, the active widget is checked directly above.
            constexpr int32_t ObjectsPerTick = 8192;
            constexpr int32_t RecentObjectsPerTick = 2048;
            const int32_t ObjectCount = SDK::UObject::GObjects->Num();
            if (ObjectCount <= 0) return nullptr;
            // Newly constructed widgets usually land near the end of GObjects.
            // Probe that small region first without waiting for a full sweep.
            for (int32_t i = std::max(0, ObjectCount - RecentObjectsPerTick);
                i < ObjectCount; ++i) {
                if (SDK::UObject* Prompt = GetActivePromptAtIndex(i, true)) {
                    CachedObjectIndex = i;
                    return Prompt;
                }
            }
            if (ScanCursor >= ObjectCount) ScanCursor = 0;
            for (int32_t Checked = 0; Checked < ObjectsPerTick && Checked < ObjectCount;
                ++Checked) {
                const int32_t i = ScanCursor++;
                if (ScanCursor >= ObjectCount) ScanCursor = 0;
                if (SDK::UObject* Prompt = GetActivePromptAtIndex(i, true)) {
                    CachedObjectIndex = i;
                    return Prompt;
                }
            }
            return nullptr;
        }

        bool IsFishingQTEKey(const SDK::FKey& Key) {
            const std::string KeyName = NormalizeGachaItemName(Key.KeyName.ToString());
            return KeyName == "q" || KeyName == "w" || KeyName == "e" ||
                KeyName == "a" || KeyName == "s" || KeyName == "d" ||
                KeyName == "z" || KeyName == "x" || KeyName == "c";
        }

        bool TryMakeFishingQTEKey(const std::string& Text, SDK::FKey* OutKey) {
            if (!OutKey) return false;
            const std::string Normalized = NormalizeGachaItemName(Text);
            if (Normalized.size() != 1 ||
                std::string("qweasdzxc").find(Normalized[0]) == std::string::npos) {
                return false;
            }

            wchar_t KeyText[2] = {
                static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(Normalized[0]))),
                L'\0'
            };
            *OutKey = SDK::FKey{};
            OutKey->KeyName = SDK::UKismetStringLibrary::Conv_StringToName(SDK::FString(KeyText));
            return IsFishingQTEKey(*OutKey);
        }

        bool TryReadFishingQTEKey(SDK::UObject* Object, SDK::FKey* OutKey, int32_t* OutKeyIndex) {
            if (!Object || !Object->Class || !OutKey || !OutKeyIndex) return false;
            *OutKey = SDK::FKey{};
            *OutKeyIndex = -1;

            SDK::FProperty* IndexProperty = FindFirstReflectedProperty(Object->Class,
                { "CurrKeyIndex", "CurrentKeyIndex", "KeyIndex", "PromptIndex", "QTEIndex",
                    "CurrentPromptIndex", "ReelKeyIndex", "FishingKeyIndex", "CurrentFishingKeyIndex" });
            if (IndexProperty && IndexProperty->ClassPrivate && IndexProperty->Offset >= 0 &&
                IndexProperty->ElementSize >= sizeof(int32_t) &&
                EqualsNameIgnoreCase(IndexProperty->ClassPrivate->Name.ToString(), "IntProperty")) {
                *OutKeyIndex = *reinterpret_cast<const int32_t*>(
                    reinterpret_cast<const uint8_t*>(Object) + IndexProperty->Offset);
            }

            auto ReadKeyValue = [&](SDK::FProperty* Property, const uint8_t* ValueAddress) -> bool {
                if (!Property || !Property->ClassPrivate || !ValueAddress) return false;
                const std::string TypeName = Property->ClassPrivate->Name.ToString();

                if (EqualsNameIgnoreCase(TypeName, "StructProperty") &&
                    Property->ElementSize >= sizeof(SDK::FKey)) {
                    SDK::FStructProperty* StructProperty = static_cast<SDK::FStructProperty*>(Property);
                    if (!StructProperty->Struct ||
                        NormalizeGachaItemName(StructProperty->Struct->GetName()).find("key") == std::string::npos) {
                        return false;
                    }
                    const SDK::FKey Candidate = *reinterpret_cast<const SDK::FKey*>(ValueAddress);
                    if (!IsFishingQTEKey(Candidate)) return false;
                    *OutKey = Candidate;
                    return true;
                }

                if (EqualsNameIgnoreCase(TypeName, "NameProperty") &&
                    Property->ElementSize >= sizeof(SDK::FName)) {
                    return TryMakeFishingQTEKey(
                        reinterpret_cast<const SDK::FName*>(ValueAddress)->ToString(), OutKey);
                }
                if (EqualsNameIgnoreCase(TypeName, "StrProperty") &&
                    Property->ElementSize >= sizeof(SDK::FString)) {
                    return TryMakeFishingQTEKey(
                        reinterpret_cast<const SDK::FString*>(ValueAddress)->ToString(), OutKey);
                }
                if (EqualsNameIgnoreCase(TypeName, "TextProperty") &&
                    Property->ElementSize >= sizeof(SDK::FText)) {
                    const SDK::FText* Text = reinterpret_cast<const SDK::FText*>(ValueAddress);
                    return Text->TextData && TryMakeFishingQTEKey(Text->ToString(), OutKey);
                }
                return false;
            };

            auto ReadSingleKey = [&](SDK::FProperty* Property) -> bool {
                if (!Property || Property->Offset < 0) return false;
                return ReadKeyValue(Property,
                    reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            };

            SDK::FProperty* NamedKeyProperty = FindFirstReflectedProperty(Object->Class,
                { "CurrKey", "CurrentKey", "KeyToPress", "RequiredKey", "PromptKey", "TargetKey",
                    "CurrentPromptKey", "CurrentFishingKey", "FishingKey", "ReelKey", "CurrentReelKey",
                    "RequiredInput", "CurrentInput", "PromptText", "KeyText" });
            if (ReadSingleKey(NamedKeyProperty)) return true;

            for (const SDK::UStruct* Current = Object->Class; Current; Current = Current->SuperStruct) {
                for (SDK::FField* Field = Current->ChildProperties; Field; Field = Field->Next) {
                    SDK::FProperty* Property = static_cast<SDK::FProperty*>(Field);
                    const std::string PropertyName = NormalizeGachaItemName(Field->Name.ToString());
                    if (PropertyName.find("key") == std::string::npos &&
                        PropertyName.find("prompt") == std::string::npos &&
                        PropertyName.find("input") == std::string::npos &&
                        PropertyName.find("button") == std::string::npos) {
                        continue;
                    }
                    if (ReadSingleKey(Property)) return true;

                    if (*OutKeyIndex < 0 || !Property->ClassPrivate || Property->Offset < 0 ||
                        !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "ArrayProperty")) {
                        continue;
                    }

                    SDK::FArrayProperty* ArrayProperty = static_cast<SDK::FArrayProperty*>(Property);
                    if (!ArrayProperty->InnerProperty || !ArrayProperty->InnerProperty->ClassPrivate ||
                        ArrayProperty->InnerProperty->ElementSize <= 0) {
                        continue;
                    }

                    struct RawKeyArray {
                        uint8_t* Data;
                        int32_t Num;
                        int32_t Max;
                    };
                    const RawKeyArray* Keys = reinterpret_cast<const RawKeyArray*>(
                        reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
                    if (!Keys->Data || Keys->Num <= 0 || Keys->Num > 128 || Keys->Max < Keys->Num ||
                        *OutKeyIndex >= Keys->Num) {
                        continue;
                    }

                    const uint8_t* ValueAddress = Keys->Data +
                        static_cast<size_t>(*OutKeyIndex) * ArrayProperty->InnerProperty->ElementSize;
                    if (ReadKeyValue(ArrayProperty->InnerProperty, ValueAddress)) return true;
                }
            }
            return false;
        }

        SDK::FProperty* FindKeyParameter(SDK::UFunction* Function) {
            if (!Function) return nullptr;

            SDK::FProperty* Fallback = nullptr;
            for (SDK::FField* Field = Function->ChildProperties; Field; Field = Field->Next) {
                SDK::FProperty* Property = static_cast<SDK::FProperty*>(Field);
                const SDK::EPropertyFlags Flags = static_cast<SDK::EPropertyFlags>(Property->PropertyFlags);
                if (!(Flags & SDK::EPropertyFlags::Parm) || (Flags & SDK::EPropertyFlags::ReturnParm)) continue;

                if (EqualsNameIgnoreCase(Field->Name.ToString(), "Key")) return Property;
                if (!Fallback && Property->ElementSize == sizeof(SDK::FKey)) Fallback = Property;
            }
            return Fallback;
        }

        bool SendExternalFishingKey(const SDK::FKey& Key, bool Pressed) {
            if (!IsFishingQTEKey(Key)) return false;
            if (Pressed && g_MDX12::g_MenuState::g_isOpen) return false;

            const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
            if (Pressed && GameWindow && GetForegroundWindow() != GameWindow) return false;

            const std::string KeyName = NormalizeGachaItemName(Key.KeyName.ToString());
            if (KeyName.size() != 1) return false;

            const UINT VirtualKey = static_cast<UINT>(
                std::toupper(static_cast<unsigned char>(KeyName[0])));
            const UINT ScanCode = MapVirtualKeyW(VirtualKey, MAPVK_VK_TO_VSC);
            if (ScanCode == 0) return false;

            INPUT Input{};
            Input.type = INPUT_KEYBOARD;
            Input.ki.wScan = static_cast<WORD>(ScanCode);
            Input.ki.dwFlags = KEYEVENTF_SCANCODE |
                (Pressed ? 0 : KEYEVENTF_KEYUP);

            const bool Sent = SendInput(1, &Input, sizeof(Input)) == 1;
            return Sent;
        }

        bool SendExternalFishingRecastClick() {
            if (g_MDX12::g_MenuState::g_isOpen) return false;
            const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
            if (GameWindow && GetForegroundWindow() != GameWindow) return false;

            INPUT Inputs[2]{};
            Inputs[0].type = INPUT_MOUSE;
            Inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            Inputs[1].type = INPUT_MOUSE;
            Inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            return SendInput(2, Inputs, sizeof(INPUT)) == 2;
        }

        bool InvokeFishingInputFunction(SDK::UObject* Target, SDK::UFunction* Function,
            const SDK::FKey& Key) {
            if (!IsUsableObject(Target) || !Function || !IsFishingQTEKey(Key)) return false;

            SDK::FProperty* KeyParameter = FindKeyParameter(Function);
            size_t ParameterSize = Function->Size > 0 ?
                static_cast<size_t>(Function->Size) : 0;
            int32_t InputParameterCount = 0;

            for (SDK::FField* Field = Function->ChildProperties; Field; Field = Field->Next) {
                SDK::FProperty* Property = static_cast<SDK::FProperty*>(Field);
                const SDK::EPropertyFlags Flags = static_cast<SDK::EPropertyFlags>(Property->PropertyFlags);
                if (!(Flags & SDK::EPropertyFlags::Parm) ||
                    (Flags & SDK::EPropertyFlags::ReturnParm)) {
                    continue;
                }

                ++InputParameterCount;
                if (Property->Offset < 0 || Property->ElementSize <= 0) return false;
                ParameterSize = std::max(ParameterSize,
                    static_cast<size_t>(Property->Offset + Property->ElementSize));

                if (Property == KeyParameter) continue;
                const std::string TypeName = Property->ClassPrivate ?
                    Property->ClassPrivate->Name.ToString() : std::string{};
                const std::string PropertyName = NormalizeGachaItemName(Field->Name.ToString());
                if (!EqualsNameIgnoreCase(TypeName, "BoolProperty") ||
                    (PropertyName.find("press") == std::string::npos &&
                        PropertyName.find("down") == std::string::npos)) {
                    return false;
                }
            }

            if (InputParameterCount > 0 && !KeyParameter && InputParameterCount != 1) return false;
            if (ParameterSize > 4096) return false;

            std::vector<uint8_t> Parameters(ParameterSize, 0);
            if (KeyParameter) {
                if (KeyParameter->Offset < 0 ||
                    static_cast<size_t>(KeyParameter->Offset) + sizeof(SDK::FKey) > Parameters.size()) {
                    return false;
                }
                std::memcpy(Parameters.data() + KeyParameter->Offset, &Key, sizeof(Key));
            }

            for (SDK::FField* Field = Function->ChildProperties; Field; Field = Field->Next) {
                SDK::FProperty* Property = static_cast<SDK::FProperty*>(Field);
                const SDK::EPropertyFlags Flags = static_cast<SDK::EPropertyFlags>(Property->PropertyFlags);
                if (!(Flags & SDK::EPropertyFlags::Parm) ||
                    (Flags & SDK::EPropertyFlags::ReturnParm) || Property == KeyParameter ||
                    !Property->ClassPrivate ||
                    !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "BoolProperty")) {
                    continue;
                }

                SDK::FBoolProperty* BoolProperty = static_cast<SDK::FBoolProperty*>(Property);
                const uint8_t Mask = BoolProperty->FieldMask ?
                    BoolProperty->FieldMask : BoolProperty->ByteMask;
                const size_t ByteOffset = static_cast<size_t>(Property->Offset) + BoolProperty->ByteOffset;
                if (!Mask || ByteOffset >= Parameters.size()) return false;
                Parameters[ByteOffset] |= Mask;
            }

            const auto OriginalFlags = Function->FunctionFlags;
            Function->FunctionFlags |= SDK::EFunctionFlags::Native;
            Target->ProcessEvent(Function, Parameters.empty() ? nullptr : Parameters.data());
            Function->FunctionFlags = OriginalFlags;
            return true;
        }

        SDK::UFunction* FindBoundFishingKeyFunction(SDK::UObject* Target,
            const SDK::FKey& Key) {
            if (!IsUsableObject(Target) || !Target->Class) return nullptr;

            static std::unordered_map<std::string, SDK::UFunction*> BindingCache;
            const std::string CacheKey = std::to_string(Target->Class->Index) + ":" +
                NormalizeGachaItemName(Key.KeyName.ToString());
            const auto Cached = BindingCache.find(CacheKey);
            if (Cached != BindingCache.end()) return Cached->second;

            std::string TargetClassName = NormalizeGachaItemName(Target->Class->GetName());
            if (TargetClassName.size() > 1 && TargetClassName.back() == 'c') {
                TargetClassName.pop_back();
            }

            SDK::UClass* BindingClass = SDK::UInputKeyDelegateBinding::StaticClass();
            if (!BindingClass) return nullptr;

            for (int32_t Index = 0; Index < SDK::UObject::GObjects->Num(); ++Index) {
                SDK::UObject* Object = SDK::UObject::GObjects->GetByIndex(Index);
                if (!IsUsableObject(Object) || !Object->IsA(BindingClass)) continue;

                const std::string BindingIdentity = NormalizeGachaItemName(Object->GetFullName());
                if (!TargetClassName.empty() &&
                    BindingIdentity.find(TargetClassName) == std::string::npos) {
                    continue;
                }

                SDK::UInputKeyDelegateBinding* KeyBinding =
                    static_cast<SDK::UInputKeyDelegateBinding*>(Object);
                for (const SDK::FBlueprintInputKeyDelegateBinding& Binding :
                    KeyBinding->InputKeyDelegateBindings) {
                    if (Binding.InputKeyEvent != SDK::EInputEvent::IE_Pressed) continue;
                    const std::string BoundKey = NormalizeGachaItemName(
                        Binding.InputChord.Key.KeyName.ToString());
                    if (Binding.InputChord.Key.KeyName != Key.KeyName && BoundKey != "anykey") continue;

                    const std::string FunctionName = Binding.FunctionNameToBind.ToString();
                    if (FunctionName.empty()) continue;
                    if (SDK::UFunction* Function = FindReflectedFunction(
                        Target->Class, FunctionName.c_str())) {
                        BindingCache.emplace(CacheKey, Function);
                        return Function;
                    }
                }
            }
            BindingCache.emplace(CacheKey, nullptr);
            return nullptr;
        }

        SDK::UFunction* FindFishingKeyEventByName(SDK::UObject* Target,
            const SDK::FKey& Key) {
            if (!IsUsableObject(Target) || !Target->Class) return nullptr;
            const std::string KeyName = NormalizeGachaItemName(Key.KeyName.ToString());
            SDK::UFunction* BestFunction = nullptr;
            int BestScore = -1;

            for (const SDK::UStruct* Current = Target->Class; Current; Current = Current->SuperStruct) {
                for (SDK::UField* Field = Current->Children; Field; Field = Field->Next) {
                    if (!Field->HasTypeFlag(SDK::EClassCastFlags::Function)) continue;
                    SDK::UFunction* Function = static_cast<SDK::UFunction*>(Field);
                    const std::string Name = NormalizeGachaItemName(Function->GetName());
                    if (Name.find("release") != std::string::npos) continue;

                    int Score = -1;
                    if (Name.find("inputkeyevent") != std::string::npos &&
                        Name.find("inpactevt" + KeyName) != std::string::npos) {
                        Score = 100;
                    }
                    else if (Name.find("inputkeyevent") != std::string::npos &&
                        Name.find("inpactevtanykey") != std::string::npos) {
                        Score = 90;
                    }
                    else if ((Name.find("fishing") != std::string::npos ||
                        Name.find("reel") != std::string::npos ||
                        Name.find("qte") != std::string::npos) &&
                        (Name.find("key") != std::string::npos ||
                            Name.find("input") != std::string::npos ||
                            Name.find("button") != std::string::npos) &&
                        FindKeyParameter(Function)) {
                        Score = 50;
                    }

                    if (Score > BestScore) {
                        BestScore = Score;
                        BestFunction = Function;
                    }
                }
            }
            return BestFunction;
        }

        bool InvokeFishingQTEEvent(SDK::UObject* FishingWeapon, SDK::UObject* PromptSource,
            const SDK::FKey& Key, std::string* OutTargetClass, std::string* OutFunctionName) {
            std::vector<SDK::UObject*> Targets;
            auto AddTarget = [&](SDK::UObject* Target) {
                if (!IsUsableObject(Target) || !Target->Class || !IsFishingRelatedObject(Target)) return;
                if (std::find(Targets.begin(), Targets.end(), Target) == Targets.end()) {
                    Targets.push_back(Target);
                }
            };

            AddTarget(FishingWeapon);
            AddTarget(PromptSource);
            for (SDK::UObject* Outer = PromptSource ? PromptSource->Outer : nullptr;
                Outer && Targets.size() < 12; Outer = Outer->Outer) {
                AddTarget(Outer);
            }

            for (SDK::UObject* Target : Targets) {
                SDK::UFunction* Function = FindBoundFishingKeyFunction(Target, Key);
                if (!Function) Function = FindFishingKeyEventByName(Target, Key);
                if (!Function || !InvokeFishingInputFunction(Target, Function, Key)) continue;

                if (OutTargetClass) *OutTargetClass = Target->Class->GetName();
                if (OutFunctionName) *OutFunctionName = Function->GetName();
                return true;
            }
            return false;
        }

        bool WriteReflectedTimeValue(SDK::UObject* Object, const char* PropertyName, double Value) {
            if (!IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0) return false;

            uint8_t* ValueAddress = reinterpret_cast<uint8_t*>(Object) + Property->Offset;
            const std::string PropertyType = Property->ClassPrivate->Name.ToString();
            if (EqualsNameIgnoreCase(PropertyType, "DoubleProperty") && Property->ElementSize >= sizeof(double)) {
                *reinterpret_cast<double*>(ValueAddress) = Value;
                return true;
            }
            if (EqualsNameIgnoreCase(PropertyType, "FloatProperty") && Property->ElementSize >= sizeof(float)) {
                *reinterpret_cast<float*>(ValueAddress) = static_cast<float>(Value);
                return true;
            }
            return false;
        }

        bool WriteReflectedInt32Value(SDK::UObject* Object, const char* PropertyName, int32_t Value) {
            if (!IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize < sizeof(int32_t)) {
                return false;
            }
            if (!EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "IntProperty")) return false;

            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(Object) + Property->Offset) = Value;
            return true;
        }

        bool ReadReflectedInt32Value(SDK::UObject* Object, const char* PropertyName, int32_t* OutValue) {
            if (!OutValue || !IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize < sizeof(int32_t) ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "IntProperty")) {
                return false;
            }

            *OutValue = *reinterpret_cast<const int32_t*>(
                reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            return true;
        }

        bool WriteReflectedBoolValue(SDK::UObject* Object, const char* PropertyName, bool Value) {
            if (!IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "BoolProperty")) {
                return false;
            }

            SDK::FBoolProperty* BoolProperty = static_cast<SDK::FBoolProperty*>(Property);
            const uint8_t Mask = BoolProperty->FieldMask ? BoolProperty->FieldMask : BoolProperty->ByteMask;
            if (!Mask || BoolProperty->ByteOffset >= sizeof(uint64_t)) return false;

            uint8_t* Byte = reinterpret_cast<uint8_t*>(Object) + Property->Offset + BoolProperty->ByteOffset;
            if (Value) *Byte |= Mask;
            else *Byte &= static_cast<uint8_t>(~Mask);
            return true;
        }

        bool ReadReflectedBoolValue(SDK::UObject* Object, const char* PropertyName, bool* OutValue) {
            if (!OutValue || !IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "BoolProperty")) {
                return false;
            }

            SDK::FBoolProperty* BoolProperty = static_cast<SDK::FBoolProperty*>(Property);
            const uint8_t Mask = BoolProperty->FieldMask ? BoolProperty->FieldMask : BoolProperty->ByteMask;
            if (!Mask || BoolProperty->ByteOffset >= sizeof(uint64_t)) return false;

            const uint8_t* Byte = reinterpret_cast<const uint8_t*>(Object) +
                Property->Offset + BoolProperty->ByteOffset;
            *OutValue = (*Byte & Mask) != 0;
            return true;
        }

        SDK::UObject* ReadReflectedObjectValue(SDK::UObject* Object, const char* PropertyName) {
            if (!IsUsableObject(Object) || !Object->Class) return nullptr;
            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize < sizeof(SDK::UObject*)) return nullptr;

            const std::string TypeName = Property->ClassPrivate->Name.ToString();
            if (!EqualsNameIgnoreCase(TypeName, "ObjectProperty") &&
                !EqualsNameIgnoreCase(TypeName, "ClassProperty")) return nullptr;

            SDK::UObject* Value = *reinterpret_cast<SDK::UObject**>(
                reinterpret_cast<uint8_t*>(Object) + Property->Offset);
            return IsUsableObject(Value) ? Value : nullptr;
        }

        bool ReadReflectedObjectArray(SDK::UObject* Object, const char* PropertyName,
            SDK::UObject*** OutData, int32_t* OutNum) {
            if (!OutData || !OutNum) return false;
            *OutData = nullptr;
            *OutNum = 0;
            if (!IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "ArrayProperty")) return false;

            SDK::FArrayProperty* ArrayProperty = static_cast<SDK::FArrayProperty*>(Property);
            if (!ArrayProperty->InnerProperty || !ArrayProperty->InnerProperty->ClassPrivate) return false;
            const std::string InnerType = ArrayProperty->InnerProperty->ClassPrivate->Name.ToString();
            // Raw UObject** iteration is only valid for a strong object array.
            // FWeakObjectPtr has a different in-memory representation.
            if (!EqualsNameIgnoreCase(InnerType, "ObjectProperty")) return false;

            struct RawObjectArray {
                SDK::UObject** Data;
                int32_t Num;
                int32_t Max;
            };
            const RawObjectArray* Array = reinterpret_cast<const RawObjectArray*>(
                reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            if (Array->Num < 0 || Array->Num > 1000000 || Array->Max < Array->Num ||
                Array->Max > 2000000 || (Array->Num > 0 && !Array->Data)) return false;

            *OutData = Array->Data;
            *OutNum = Array->Num;
            return true;
        }

        bool ReadReflectedNumberValue(SDK::UObject* Object, const char* PropertyName, double* OutValue) {
            if (!OutValue || !IsUsableObject(Object) || !Object->Class) return false;
            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0) return false;

            const uint8_t* Address = reinterpret_cast<const uint8_t*>(Object) + Property->Offset;
            const std::string TypeName = Property->ClassPrivate->Name.ToString();
            if (EqualsNameIgnoreCase(TypeName, "DoubleProperty") && Property->ElementSize >= sizeof(double)) {
                *OutValue = *reinterpret_cast<const double*>(Address);
            }
            else if (EqualsNameIgnoreCase(TypeName, "FloatProperty") && Property->ElementSize >= sizeof(float)) {
                *OutValue = static_cast<double>(*reinterpret_cast<const float*>(Address));
            }
            else {
                return false;
            }
            return std::isfinite(*OutValue);
        }

        bool ReadReflectedFloatValue(SDK::UObject* Object, const char* PropertyName, float* OutValue) {
            if (!OutValue || !IsUsableObject(Object) || !Object->Class) return false;
            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize < sizeof(float) ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "FloatProperty")) {
                return false;
            }
            *OutValue = *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            return std::isfinite(*OutValue);
        }

        bool ReadReflectedByteValue(SDK::UObject* Object, const char* PropertyName, uint8_t* OutValue) {
            if (!OutValue || !IsUsableObject(Object) || !Object->Class) return false;
            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 || Property->ElementSize < 1) {
                return false;
            }
            const std::string TypeName = Property->ClassPrivate->Name.ToString();
            if (!EqualsNameIgnoreCase(TypeName, "ByteProperty") &&
                !EqualsNameIgnoreCase(TypeName, "EnumProperty")) {
                return false;
            }
            *OutValue = *(reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            return true;
        }

        float* GetReflectedFloatAddress(SDK::UObject* Object, const char* PropertyName) {
            if (!IsUsableObject(Object) || !Object->Class) return nullptr;
            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize < sizeof(float) ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "FloatProperty")) {
                return nullptr;
            }
            return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(Object) + Property->Offset);
        }

        float* GetReflectedNestedFloatAddress(SDK::UObject* Object, const char* StructPropertyName,
            const char* FloatPropertyName) {
            if (!IsUsableObject(Object) || !Object->Class) return nullptr;
            SDK::FProperty* OuterProperty = FindReflectedProperty(Object->Class, StructPropertyName);
            if (!OuterProperty || !OuterProperty->ClassPrivate || OuterProperty->Offset < 0 ||
                OuterProperty->ArrayDim != 1 || OuterProperty->ElementSize < int(sizeof(float)) ||
                OuterProperty->ElementSize > Object->Class->Size ||
                OuterProperty->Offset > Object->Class->Size - OuterProperty->ElementSize ||
                !EqualsNameIgnoreCase(OuterProperty->ClassPrivate->Name.ToString(), "StructProperty")) {
                return nullptr;
            }

            SDK::FStructProperty* StructProperty = static_cast<SDK::FStructProperty*>(OuterProperty);
            if (!StructProperty->Struct) return nullptr;
            SDK::FProperty* InnerProperty = FindReflectedProperty(StructProperty->Struct, FloatPropertyName);
            if (!InnerProperty || !InnerProperty->ClassPrivate || InnerProperty->Offset < 0 ||
                InnerProperty->ArrayDim != 1 || InnerProperty->ElementSize != sizeof(float) ||
                InnerProperty->Offset > OuterProperty->ElementSize - int(sizeof(float)) ||
                !EqualsNameIgnoreCase(InnerProperty->ClassPrivate->Name.ToString(), "FloatProperty")) {
                return nullptr;
            }

            return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(Object) + OuterProperty->Offset +
                InnerProperty->Offset);
        }

        bool ReadReflectedStructBool(const uint8_t* Element, int32_t ElementSize,
            SDK::FProperty* Property, bool* OutValue) {
            if (!Element || !Property || !OutValue || !Property->ClassPrivate || Property->Offset < 0 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "BoolProperty")) {
                return false;
            }
            SDK::FBoolProperty* BoolProperty = static_cast<SDK::FBoolProperty*>(Property);
            const uint8_t Mask = BoolProperty->FieldMask ? BoolProperty->FieldMask : BoolProperty->ByteMask;
            const int32_t ByteIndex = Property->Offset + BoolProperty->ByteOffset;
            if (!Mask || ByteIndex < 0 || ByteIndex >= ElementSize) return false;
            *OutValue = (Element[ByteIndex] & Mask) != 0;
            return true;
        }

        struct ReflectedStructArrayView {
            uint8_t* Data = nullptr;
            int32_t Num = 0;
            int32_t Max = 0;
            int32_t ElementSize = 0;
            SDK::UStruct* ElementStruct = nullptr;
        };

        struct ReflectedStructValueView {
            uint8_t* Data = nullptr;
            int32_t Size = 0;
            SDK::UStruct* Type = nullptr;
        };

        struct RawArrayHeader {
            uint8_t* Data = nullptr;
            int32_t Num = 0;
            int32_t Max = 0;
        };

        bool GetReflectedStructValue(SDK::UObject* Object, const char* PropertyName,
            ReflectedStructValueView* OutView) {
            if (!OutView) return false;
            *OutView = ReflectedStructValueView{};
            if (!IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize <= 0 || Property->ElementSize > 0x100000 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "StructProperty")) {
                return false;
            }

            SDK::FStructProperty* StructProperty = static_cast<SDK::FStructProperty*>(Property);
            if (!StructProperty->Struct) return false;
            OutView->Data = reinterpret_cast<uint8_t*>(Object) + Property->Offset;
            OutView->Size = Property->ElementSize;
            OutView->Type = StructProperty->Struct;
            return true;
        }

        bool GetNestedReflectedStructValue(const ReflectedStructValueView& Parent,
            const char* PropertyName, ReflectedStructValueView* OutView) {
            if (!OutView) return false;
            *OutView = ReflectedStructValueView{};
            if (!Parent.Data || Parent.Size <= 0 || !Parent.Type) return false;

            SDK::FProperty* Property = FindReflectedProperty(Parent.Type, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->ElementSize <= 0 || Property->ElementSize > Parent.Size ||
                Property->Offset + Property->ElementSize > Parent.Size ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "StructProperty")) {
                return false;
            }

            SDK::FStructProperty* StructProperty = static_cast<SDK::FStructProperty*>(Property);
            if (!StructProperty->Struct) return false;
            OutView->Data = Parent.Data + Property->Offset;
            OutView->Size = Property->ElementSize;
            OutView->Type = StructProperty->Struct;
            return true;
        }

        bool GetReflectedArrayHeader(const ReflectedStructValueView& Parent,
            const char* PropertyName, const char* ExpectedInnerType,
            int32_t MaximumReasonableCount, RawArrayHeader* OutHeader,
            int32_t* OutElementSize = nullptr) {
            if (!OutHeader) return false;
            *OutHeader = RawArrayHeader{};
            if (OutElementSize) *OutElementSize = 0;
            if (!Parent.Data || Parent.Size <= 0 || !Parent.Type) return false;

            SDK::FProperty* Property = FindReflectedProperty(Parent.Type, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                Property->Offset + static_cast<int32_t>(sizeof(RawArrayHeader)) > Parent.Size ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "ArrayProperty")) {
                return false;
            }

            SDK::FArrayProperty* ArrayProperty = static_cast<SDK::FArrayProperty*>(Property);
            if (!ArrayProperty->InnerProperty || !ArrayProperty->InnerProperty->ClassPrivate ||
                ArrayProperty->InnerProperty->ElementSize <= 0 ||
                !EqualsNameIgnoreCase(ArrayProperty->InnerProperty->ClassPrivate->Name.ToString(),
                    ExpectedInnerType)) {
                return false;
            }

            const RawArrayHeader* Header = reinterpret_cast<const RawArrayHeader*>(
                Parent.Data + Property->Offset);
            if (Header->Num < 0 || Header->Num > MaximumReasonableCount ||
                Header->Max < Header->Num || Header->Max > MaximumReasonableCount * 4 ||
                (Header->Num > 0 && !Header->Data)) {
                return false;
            }

            *OutHeader = *Header;
            if (OutElementSize) *OutElementSize = ArrayProperty->InnerProperty->ElementSize;
            return true;
        }

        bool GetReflectedStructArray(SDK::UObject* Object, const char* PropertyName,
            int32_t MaximumReasonableCount, ReflectedStructArrayView* OutView) {
            if (!OutView) return false;
            *OutView = ReflectedStructArrayView{};
            if (!IsUsableObject(Object) || !Object->Class) return false;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "ArrayProperty")) {
                return false;
            }

            SDK::FArrayProperty* ArrayProperty = static_cast<SDK::FArrayProperty*>(Property);
            if (!ArrayProperty->InnerProperty || !ArrayProperty->InnerProperty->ClassPrivate ||
                !EqualsNameIgnoreCase(ArrayProperty->InnerProperty->ClassPrivate->Name.ToString(), "StructProperty")) {
                return false;
            }

            SDK::FStructProperty* StructProperty =
                static_cast<SDK::FStructProperty*>(ArrayProperty->InnerProperty);
            if (!StructProperty->Struct || ArrayProperty->InnerProperty->ElementSize <= 0 ||
                ArrayProperty->InnerProperty->ElementSize > 0x10000) {
                return false;
            }

            struct RawArrayHeader {
                uint8_t* Data;
                int32_t Num;
                int32_t Max;
            };
            const RawArrayHeader* Header = reinterpret_cast<const RawArrayHeader*>(
                reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            if (Header->Num < 0 || Header->Num > MaximumReasonableCount ||
                Header->Max < Header->Num || Header->Max > MaximumReasonableCount * 4 ||
                (Header->Num > 0 && !Header->Data)) {
                return false;
            }

            OutView->Data = Header->Data;
            OutView->Num = Header->Num;
            OutView->Max = Header->Max;
            OutView->ElementSize = ArrayProperty->InnerProperty->ElementSize;
            OutView->ElementStruct = StructProperty->Struct;
            return true;
        }

        SDK::AShooterCharacter* GetLocalHumanCharacter(SDK::UWorld* World) {
            if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
                return nullptr;
            }

            SDK::ULocalPlayer* LocalPlayer = World->OwningGameInstance->LocalPlayers[0];
            if (!LocalPlayer || !LocalPlayer->PlayerController ||
                !LocalPlayer->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) {
                return nullptr;
            }

            SDK::AShooterPlayerController* PC =
                static_cast<SDK::AShooterPlayerController*>(LocalPlayer->PlayerController);
            if (PC->Character && PC->Character->IsA(SDK::AShooterCharacter::StaticClass()) &&
                !PC->Character->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return static_cast<SDK::AShooterCharacter*>(PC->Character);
            }

            if (PC->Pawn && PC->Pawn->IsA(SDK::AShooterCharacter::StaticClass()) &&
                !PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return static_cast<SDK::AShooterCharacter*>(PC->Pawn);
            }

            return nullptr;
        }

        SDK::AShooterPlayerController* GetLocalShooterController(SDK::UWorld* World) {
            if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
                return nullptr;
            }
            SDK::ULocalPlayer* LocalPlayer = World->OwningGameInstance->LocalPlayers[0];
            if (!LocalPlayer || !LocalPlayer->PlayerController ||
                !LocalPlayer->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) {
                return nullptr;
            }
            return static_cast<SDK::AShooterPlayerController*>(LocalPlayer->PlayerController);
        }

        SDK::AShooterCharacter* GetLocalConsumableCharacter(SDK::UWorld* World) {
            SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
            if (!IsUsableObject(PC)) return nullptr;

            // On a possessed mount, use its actual rider. Validate the weak
            // reference's serial before dereferencing it: Get() alone can
            // return a different object after the survivor is replaced.
            SDK::APrimalDinoCharacter* Mount = nullptr;
            if (IsUsableObject(PC->Pawn) &&
                PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                Mount = static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn);
            }
            if (Mount && Mount->Controller == PC) {
                const auto& RiderRef = Mount->Rider;
                SDK::AShooterCharacter* Rider = RiderRef.Get();
                int32_t LiveSerial = 0;
                if (RiderRef.ObjectSerialNumber > 0 &&
                    TryGetLiveObjectSerial(Rider, RiderRef.ObjectIndex, &LiveSerial) &&
                    LiveSerial == RiderRef.ObjectSerialNumber &&
                    IsUsableObject(Rider) &&
                    Rider->IsA(SDK::AShooterCharacter::StaticClass()) &&
                    !Rider->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                    return Rider;
                }
            }

            // Possession moves to the mount while riding. The controller's
            // player character remains the survivor whose inventory and stats
            // must be used for food, water and brews.
            SDK::AShooterCharacter* Human = PC->GetPlayerCharacter();
            if (IsUsableObject(Human) &&
                Human->IsA(SDK::AShooterCharacter::StaticClass()) &&
                !Human->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return Human;
            }
            return GetLocalHumanCharacter(World);
        }

        struct AutoCraftRuntimeState {
            SDK::UWorld* World = nullptr;
            std::vector<g_Hack::AutoCraftStationView> Catalog;
            int SelectedInventoryObjectIndex = -1;
            int SelectedItemObjectIndex = -1;
            SDK::UPrimalInventoryComponent* SelectedInventory = nullptr;
            int32_t SelectedInventorySerial = -1;
            SDK::FItemNetID SelectedRecipeID{};
            std::string SelectedRecipeClass;
            bool SelectedRecipeIsEngram = false;
            bool OwnsCraftRemoteView = false;
            SDK::UUI_Inventory* BatchInventoryUI = nullptr;
            int32_t BatchUIIndex = -1;
            int32_t BatchUISerial = -1;
            uint64_t SubmittedRequests = 0;
            std::chrono::steady_clock::time_point NextInventorySync =
                std::chrono::steady_clock::time_point::min();
            g_Hack::AutoCraftScheduler Scheduler{};
            SDK::UPrimalInventoryComponent* DiscoveryInventory = nullptr;
            int DiscoveryInventoryObjectIndex = -1;
            std::chrono::steady_clock::time_point DiscoveryRequestedAt =
                std::chrono::steady_clock::time_point::min();
            std::chrono::steady_clock::time_point NextDiscoveryRequest =
                std::chrono::steady_clock::time_point::min();
            std::string Status = "请选择工作台和印痕/图纸";
            std::chrono::steady_clock::time_point NextCatalogRefresh =
                std::chrono::steady_clock::time_point::min();
        };

        AutoCraftRuntimeState g_AutoCraftRuntime;

        // ImGui runs on the render thread while all inventory RPCs and UObject
        // traversal must stay on the game thread. This bridge contains only
        // value snapshots and queued integer selections.
        struct AutoCraftUiBridgeState {
            std::mutex RequestMutex;
            bool StationSelectionPending = false;
            int PendingStationObjectIndex = -1;
            bool RecipeSelectionPending = false;
            int PendingRecipeObjectIndex = -1;

            std::mutex SnapshotMutex;
            std::vector<g_Hack::AutoCraftStationView> Catalog;
            int SelectedStationObjectIndex = -1;
            int SelectedRecipeObjectIndex = -1;
            std::string Status = "请选择工作台和印痕/图纸";
        };

        AutoCraftUiBridgeState g_AutoCraftUiBridge;

        void PublishAutoCraftUiSnapshot() {
            std::scoped_lock Lock(g_AutoCraftUiBridge.SnapshotMutex);
            g_AutoCraftUiBridge.Catalog = g_AutoCraftRuntime.Catalog;
            g_AutoCraftUiBridge.SelectedStationObjectIndex =
                g_AutoCraftRuntime.SelectedInventoryObjectIndex;
            g_AutoCraftUiBridge.SelectedRecipeObjectIndex =
                g_AutoCraftRuntime.SelectedItemObjectIndex;
            g_AutoCraftUiBridge.Status = g_AutoCraftRuntime.Status;
        }

        struct AutoCraftUiSnapshotScope {
            ~AutoCraftUiSnapshotScope() {
                PublishAutoCraftUiSnapshot();
            }
        };

        std::string g_AutoFeedStatus = "等待范围内恐龙";

        struct DaeodonHealingRequestState {
            std::mutex Mutex;
            bool Pending = false;
            std::string Status = "等待批量开启请求";
        };

        DaeodonHealingRequestState g_DaeodonHealingRequest;

        struct AutoGachaPullRuntimeState {
            SDK::UWorld* World = nullptr;
            SDK::UPrimalInventoryComponent* TargetInventory = nullptr;
            int32_t TargetInventoryObjectIndex = -1;
            std::chrono::steady_clock::time_point ViewRequestedAt =
                std::chrono::steady_clock::time_point::min();
            std::chrono::steady_clock::time_point NextAction =
                std::chrono::steady_clock::time_point::min();
            std::unordered_map<int32_t, std::chrono::steady_clock::time_point> RetryAfter;
            std::string Status = "等待附近可访问的保险箱";
        };

        AutoGachaPullRuntimeState g_AutoGachaPullRuntime;

        SDK::UPrimalItem* FindGachaCrystalInInventory(SDK::UPrimalInventoryComponent* Inventory) {
            if (!IsUsableObject(Inventory)) return nullptr;

            SDK::TArray<SDK::UPrimalItem*>& Items = Inventory->InventoryItems;
            if (Items.Num() < 0 || Items.Num() > 4096) return nullptr;
            for (int32_t Index = 0; Index < Items.Num(); ++Index) {
                SDK::UPrimalItem* Item = Items[Index];
                if (IsUsableObject(Item) && IsGachaCrystal(Item)) return Item;
            }
            return nullptr;
        }

        bool IsGachaCrystalVault(SDK::APrimalStructureItemContainer* Container) {
            if (!IsUsableObject(Container) || !Container->Class) return false;

            const std::string DisplayName = Container->GetDescriptiveName().ToString();
            if (DisplayName.find("保险箱") != std::string::npos) return true;

            std::string Identity = Container->Class->GetName();
            Identity += Container->GetName();
            Identity += DisplayName;
            Identity = NormalizeGachaItemName(std::move(Identity));
            return Identity.find("storageboxhuge") != std::string::npos ||
                Identity.find("structurevault") != std::string::npos ||
                Identity.find("largevault") != std::string::npos ||
                Identity.find("vault") != std::string::npos;
        }

        void ClearAutoGachaTarget(SDK::AShooterPlayerController* PC, bool CloseRemoteView,
            std::chrono::steady_clock::time_point RetryAt = std::chrono::steady_clock::time_point::min()) {
            SDK::UPrimalInventoryComponent* Target = ResolveCachedObject(
                g_AutoGachaPullRuntime.TargetInventory,
                g_AutoGachaPullRuntime.TargetInventoryObjectIndex);

            if (CloseRemoteView && PC && Target) {
                // Close the server-side remote-view session even when no local
                // inventory UI was created for it.
                PC->ServerActorCloseRemoteInventory(Target);
            }
            if (RetryAt != std::chrono::steady_clock::time_point::min() &&
                g_AutoGachaPullRuntime.TargetInventoryObjectIndex >= 0) {
                g_AutoGachaPullRuntime.RetryAfter[
                    g_AutoGachaPullRuntime.TargetInventoryObjectIndex] = RetryAt;
            }

            g_AutoGachaPullRuntime.TargetInventory = nullptr;
            g_AutoGachaPullRuntime.TargetInventoryObjectIndex = -1;
            g_AutoGachaPullRuntime.ViewRequestedAt = std::chrono::steady_clock::time_point::min();
        }

        void ProcessAutoGachaVaultPull(SDK::UWorld* World, SDK::AShooterPlayerController* PC,
            SDK::AShooterCharacter* Human) {
            if (IsUploadTerminalActive() || IsAutomaticDepositActive()) return;
            using Clock = std::chrono::steady_clock;
            const Clock::time_point Now = Clock::now();

            if (g_AutoGachaPullRuntime.World != World) {
                g_AutoGachaPullRuntime = AutoGachaPullRuntimeState{};
                g_AutoGachaPullRuntime.World = World;
            }

            if (!g_Config::bAutoPullGachaCrystals) {
                ClearAutoGachaTarget(PC, true);
                g_AutoGachaPullRuntime.RetryAfter.clear();
                g_AutoGachaPullRuntime.Status = "保险箱自动取水晶未启用";
                return;
            }
            if (!World || !PC || !Human) {
                g_AutoGachaPullRuntime.Status = "等待人物和世界数据";
                return;
            }
            if (Now < g_AutoGachaPullRuntime.NextAction) return;
            g_AutoGachaPullRuntime.NextAction = Now + std::chrono::milliseconds(250);

            SDK::UPrimalInventoryComponent* Target = ResolveCachedObject(
                g_AutoGachaPullRuntime.TargetInventory,
                g_AutoGachaPullRuntime.TargetInventoryObjectIndex);
            SDK::UPrimalInventoryComponent* ViewingInventory = PC->GetViewingInventory();

            if (g_AutoGachaPullRuntime.TargetInventoryObjectIndex >= 0 && !Target) {
                ClearAutoGachaTarget(PC, false);
                g_AutoGachaPullRuntime.Status = "目标保险箱已失效，正在重新搜索";
                return;
            }

            if (Target) {
                if (ViewingInventory && ViewingInventory != Target) {
                    ClearAutoGachaTarget(PC, true, Now + std::chrono::seconds(5));
                    g_AutoGachaPullRuntime.Status = "检测到手动打开的其他容器，已暂停取水晶";
                    return;
                }

                const auto ViewElapsed = Now - g_AutoGachaPullRuntime.ViewRequestedAt;
                if (ViewElapsed < std::chrono::milliseconds(500)) {
                    g_AutoGachaPullRuntime.Status = "正在后台建立保险箱访问，不需要手动打开界面";
                    return;
                }

                if (FindGachaCrystalInInventory(Target)) {
                    // Use the same server RPC as the inventory's Transfer All
                    // button. Dedicated servers match CurrentNameFilter against
                    // the English item name even when the client UI is Chinese,
                    // so never pass the localized DescriptiveNameBase here.
                    const SDK::FString EmptyFilter(L"");
                    const SDK::FString GachaCrystalNameFilter(L"Gacha Crystal");
                    PC->ServerTransferAllFromRemoteInventory(
                        Target, EmptyFilter, GachaCrystalNameFilter,
                        EmptyFilter, true);
                    g_AutoGachaPullRuntime.Status =
                        "已使用英文过滤 Gacha Crystal 请求批量转移全部水晶";
                    // Reliable RPC ordering makes the transfer run before the
                    // remote-view session is closed. The item goes directly to
                    // the character inventory without keeping the vault UI open.
                    ClearAutoGachaTarget(PC, true, Now + std::chrono::seconds(5));
                    return;
                }

                // Give replicated inventory items time to arrive before an
                // empty result is accepted.
                if (ViewElapsed < std::chrono::seconds(2)) {
                    g_AutoGachaPullRuntime.Status = "正在后台同步保险箱物品";
                    return;
                }

                ClearAutoGachaTarget(PC, true, Now + std::chrono::seconds(10));
                g_AutoGachaPullRuntime.Status = "该保险箱没有水晶，或服务器拒绝了后台访问";
                return;
            }

            // Never replace an inventory the player opened manually.
            if (ViewingInventory) {
                g_AutoGachaPullRuntime.Status = "正在手动查看容器，自动取水晶已暂停";
                return;
            }

            SDK::UClass* ContainerClass = SDK::APrimalStructureItemContainer::StaticClass();
            SDK::UClass* InventoryClass = SDK::UPrimalInventoryComponent::StaticClass();
            if (!ContainerClass || !InventoryClass) return;

            SDK::TArray<SDK::AActor*> Actors;
            SDK::UGameplayStatics::GetAllActorsOfClass(World, ContainerClass, &Actors);
            if (Actors.Num() < 0 || Actors.Num() > 100000) return;

            int32_t PlayerTeam = 0;
            const bool HasPlayerTeam = ReadReflectedInt32Value(Human, "TargetingTeam", &PlayerTeam);
            const float MaximumRange = std::clamp(
                g_Config::AutoPullGachaRangeMeters, 2.0f, 30.0f);
            SDK::UPrimalInventoryComponent* BestInventory = nullptr;
            float BestDistance = INFINITY;

            for (int32_t Index = 0; Index < Actors.Num(); ++Index) {
                SDK::AActor* Actor = Actors[Index];
                if (!IsUsableObject(Actor) || !Actor->IsA(ContainerClass)) continue;
                SDK::APrimalStructureItemContainer* Container =
                    static_cast<SDK::APrimalStructureItemContainer*>(Actor);
                if (!IsGachaCrystalVault(Container)) continue;

                const float Distance = Human->GetDistanceTo(Container) * 0.01f;
                if (!std::isfinite(Distance) || Distance > MaximumRange || Distance >= BestDistance) continue;
                int32_t ContainerTeam = 0;
                if (HasPlayerTeam &&
                    ReadReflectedInt32Value(Container, "TargetingTeam", &ContainerTeam) &&
                    ContainerTeam != PlayerTeam) {
                    continue;
                }

                SDK::UObject* InventoryObject = ReadReflectedObjectValue(Container, "MyInventoryComponent");
                if (!IsUsableObject(InventoryObject) || !InventoryObject->IsA(InventoryClass)) continue;
                SDK::UPrimalInventoryComponent* Inventory =
                    static_cast<SDK::UPrimalInventoryComponent*>(InventoryObject);

                const auto Retry = g_AutoGachaPullRuntime.RetryAfter.find(Inventory->Index);
                if (Retry != g_AutoGachaPullRuntime.RetryAfter.end() && Now < Retry->second) continue;
                BestInventory = Inventory;
                BestDistance = Distance;
            }

            if (!BestInventory) {
                g_AutoGachaPullRuntime.Status = "范围内没有可尝试访问的保险箱";
                return;
            }

            PC->ServerActorViewRemoteInventory(BestInventory);
            PC->ServerRequestActorItems(BestInventory, true, false);
            g_AutoGachaPullRuntime.TargetInventory = BestInventory;
            g_AutoGachaPullRuntime.TargetInventoryObjectIndex = BestInventory->Index;
            g_AutoGachaPullRuntime.ViewRequestedAt = Now;
            g_AutoGachaPullRuntime.Status = "正在后台访问附近保险箱（服务器将校验权限和距离）";
        }

        void RefreshAutoCraftCatalog(SDK::UWorld* World) {
            using Clock = std::chrono::steady_clock;
            const Clock::time_point Now = Clock::now();

            if (g_AutoCraftRuntime.World != World) {
                g_AutoCraftRuntime = AutoCraftRuntimeState{};
                g_AutoCraftRuntime.World = World;
            }
            if (!World || Now < g_AutoCraftRuntime.NextCatalogRefresh) return;
            g_AutoCraftRuntime.NextCatalogRefresh = Now + std::chrono::milliseconds(750);

            SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
            SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
            SDK::UClass* StructureClass = SDK::APrimalStructure::StaticClass();
            SDK::UClass* InventoryClass = SDK::UPrimalInventoryComponent::StaticClass();
            SDK::UClass* ItemClass = SDK::UPrimalItem::StaticClass();
            if (!PC || !Human || !StructureClass || !InventoryClass || !ItemClass) {
                g_AutoCraftRuntime.Catalog.clear();
                return;
            }

            SDK::UPrimalInventoryComponent* DiscoveryInventory = ResolveCachedObject(
                g_AutoCraftRuntime.DiscoveryInventory,
                g_AutoCraftRuntime.DiscoveryInventoryObjectIndex);
            if (g_AutoCraftRuntime.DiscoveryInventory && !DiscoveryInventory) {
                g_AutoCraftRuntime.DiscoveryInventory = nullptr;
                g_AutoCraftRuntime.DiscoveryInventoryObjectIndex = -1;
                g_AutoCraftRuntime.DiscoveryRequestedAt = Clock::time_point::min();
                g_AutoCraftRuntime.NextDiscoveryRequest = Clock::time_point::min();
            }

            // The first reply can contain only the ordinary inventory and omit
            // the hidden default engrams. Retry at a low rate while the remote
            // view is alive; bWithFirstSpawn asks for those default entries.
            if (DiscoveryInventory && Now >= g_AutoCraftRuntime.NextDiscoveryRequest) {
                PC->ServerRequestActorItems(DiscoveryInventory, true, true);
                g_AutoCraftRuntime.NextDiscoveryRequest = Now + std::chrono::seconds(2);
            }

            SDK::TArray<SDK::AActor*> Actors;
            // Scan the PrimalStructure base instead of only
            // APrimalStructureItemContainer. Several mod benches and newer
            // crafting structures keep an inventory on a different subclass.
            SDK::UGameplayStatics::GetAllActorsOfClass(World, StructureClass, &Actors);
            if (Actors.Num() < 0 || Actors.Num() > 100000) {
                g_AutoCraftRuntime.Catalog.clear();
                return;
            }

            const float MaximumRange = std::clamp(g_Config::AutoCraftRangeMeters, 2.0f, 100.0f);
            std::vector<g_Hack::AutoCraftStationView> NewCatalog;
            NewCatalog.reserve(std::min(Actors.Num(), 128));

            for (int32_t ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex) {
                SDK::AActor* Actor = Actors[ActorIndex];
                if (!IsUsableObject(Actor) || !Actor->IsA(StructureClass)) continue;

                const float DistanceMeters = Human->GetDistanceTo(Actor) * 0.01f;
                if (!std::isfinite(DistanceMeters) || DistanceMeters > MaximumRange) continue;

                SDK::UObject* InventoryObject = ReadReflectedObjectValue(Actor, "MyInventoryComponent");
                if (!InventoryObject) InventoryObject = ReadReflectedObjectValue(Actor, "InventoryComponent");
                if (!InventoryObject) InventoryObject = ReadReflectedObjectValue(Actor, "PrimalInventoryComponent");
                if (!InventoryObject || !InventoryObject->IsA(InventoryClass)) continue;
                SDK::UPrimalInventoryComponent* Inventory =
                    static_cast<SDK::UPrimalInventoryComponent*>(InventoryObject);

                SDK::TArray<SDK::UPrimalItem*>& InventoryItems = Inventory->InventoryItems;
                const int32_t ItemCount = InventoryItems.Num();
                if (ItemCount < 0 || ItemCount > 100000) continue;

                g_Hack::AutoCraftStationView Station;
                Station.StationObjectIndex = Actor->Index;
                Station.InventoryObjectIndex = Inventory->Index;
                Station.DistanceMeters = DistanceMeters;
                Station.Name = static_cast<SDK::APrimalStructure*>(Actor)->GetDescriptiveName().ToString();
                if (Station.Name.empty() || Station.Name == "None") Station.Name = Actor->Class->GetName();

                std::unordered_set<SDK::UClass*> DefaultEngramClasses;
                auto CollectDefaultEngramClasses = [&DefaultEngramClasses, Inventory](
                    const SDK::TArray<SDK::TSubclassOf<SDK::UPrimalItem>>& Classes,
                    const SDK::TArray<uint8_t>& EngramFlags) {
                    const int32_t ClassCount = Classes.Num();
                    const int32_t FlagCount = EngramFlags.Num();
                    if (ClassCount < 0 || ClassCount > 100000 ||
                        FlagCount < 0 || FlagCount > 100000) {
                        return;
                    }
                    for (int32_t Index = 0; Index < ClassCount; ++Index) {
                        const bool IsDefaultEngram = Inventory->bAllDefaultInventoryIsEngrams ||
                            (Index < FlagCount && EngramFlags[Index] != 0);
                        SDK::UClass* ItemClass = Classes[Index];
                        if (IsDefaultEngram && ItemClass) DefaultEngramClasses.insert(ItemClass);
                    }
                };
                CollectDefaultEngramClasses(Inventory->DefaultInventoryItems, Inventory->DefaultEngrams);
                CollectDefaultEngramClasses(Inventory->DefaultInventoryItems2, Inventory->DefaultEngrams2);
                CollectDefaultEngramClasses(Inventory->DefaultInventoryItems3, Inventory->DefaultEngrams3);
                CollectDefaultEngramClasses(Inventory->DefaultInventoryItems4, Inventory->DefaultEngrams4);

                std::unordered_set<std::string> SeenRecipes;
                for (int32_t ItemIndex = 0; ItemIndex < ItemCount && ItemIndex < 2048; ++ItemIndex) {
                    SDK::UObject* ItemObject = InventoryItems[ItemIndex];
                    if (!IsUsableObject(ItemObject) || !ItemObject->IsA(ItemClass)) continue;
                    SDK::UPrimalItem* Item = static_cast<SDK::UPrimalItem*>(ItemObject);

                    bool IsEngram = Item->bIsEngram;
                    bool IsBlueprint = Item->bIsBlueprint;
                    bool ReflectedValue = false;
                    if (ReadReflectedBoolValue(Item, "bIsEngram", &ReflectedValue)) {
                        IsEngram = ReflectedValue;
                    }
                    if (ReadReflectedBoolValue(Item, "bIsBlueprint", &ReflectedValue)) {
                        IsBlueprint = ReflectedValue;
                    }
                    const bool MatchesDefaultEngram = Item->Class &&
                        DefaultEngramClasses.contains(Item->Class);
                    if (!IsEngram && !IsBlueprint && !MatchesDefaultEngram) continue;

                    std::string RecipeName = Item->GetItemShortName().ToString();
                    if (RecipeName.empty() || RecipeName == "None") {
                        RecipeName = Item->DescriptiveNameBase.ToString();
                    }
                    if (RecipeName.empty() || RecipeName == "None") RecipeName = Item->Class->GetName();

                    const std::string UniqueRecipe = NormalizeGachaItemName(RecipeName) +
                        std::to_string(IsBlueprint ? 1 : 0);
                    if (!SeenRecipes.insert(UniqueRecipe).second) continue;
                    Station.Recipes.push_back({ Item->Index, RecipeName, IsBlueprint });
                }

                // Keep crafting/grinding-capable stations even before their
                // hidden default recipe items have been replicated. Selecting
                // one triggers a short background inventory request below.
                const bool SupportsCrafting = Inventory->RemoteInventoryAllowCraftingItems(PC, true);
                const bool SupportsGrinding = Inventory->CanGrindItems(PC);
                auto HasEngramFlag = [](const SDK::TArray<uint8_t>& Flags) {
                    const int32_t Count = Flags.Num();
                    if (Count < 0 || Count > 100000) return false;
                    for (int32_t Index = 0; Index < Count; ++Index) {
                        if (Flags[Index] != 0) return true;
                    }
                    return false;
                };
                const bool HasDefaultEngramDefinitions = !DefaultEngramClasses.empty() ||
                    HasEngramFlag(Inventory->DefaultEngrams) ||
                    HasEngramFlag(Inventory->DefaultEngrams2) ||
                    HasEngramFlag(Inventory->DefaultEngrams3) ||
                    HasEngramFlag(Inventory->DefaultEngrams4);
                std::string StationIdentity = Station.Name + Actor->Class->GetName() +
                    Inventory->Class->GetName();
                StationIdentity = NormalizeGachaItemName(std::move(StationIdentity));
                const bool HasWorkstationName =
                    StationIdentity.find("grinder") != std::string::npos ||
                    StationIdentity.find("grinding") != std::string::npos ||
                    StationIdentity.find("chemistry") != std::string::npos ||
                    StationIdentity.find("chembench") != std::string::npos ||
                    StationIdentity.find("workbench") != std::string::npos ||
                    StationIdentity.find("craftingstation") != std::string::npos ||
                    StationIdentity.find("fabricator") != std::string::npos ||
                    StationIdentity.find("smithy") != std::string::npos ||
                    StationIdentity.find("forge") != std::string::npos ||
                    StationIdentity.find("mortar") != std::string::npos ||
                    StationIdentity.find("replicator") != std::string::npos;
                const bool HasStorageName =
                    StationIdentity.find("dedicatedstorage") != std::string::npos ||
                    StationIdentity.find("storagebox") != std::string::npos ||
                    StationIdentity.find("storagecontainer") != std::string::npos ||
                    StationIdentity.find("storage") != std::string::npos ||
                    StationIdentity.find("vault") != std::string::npos ||
                    StationIdentity.find("bookshelf") != std::string::npos ||
                    StationIdentity.find("preservingbin") != std::string::npos ||
                    StationIdentity.find("refrigerator") != std::string::npos ||
                    StationIdentity.find("fridge") != std::string::npos;
                // Some storage inventories report the generic remote-crafting
                // permission as true. Treat an unmistakable storage class as a
                // container unless it supplies real recipes/default engrams or
                // is also explicitly identifiable as a workstation.
                if (Station.Recipes.empty() && HasStorageName &&
                    !HasDefaultEngramDefinitions && !HasWorkstationName && !SupportsGrinding) {
                    continue;
                }
                if (Station.Recipes.empty() && !SupportsCrafting && !SupportsGrinding &&
                    !HasDefaultEngramDefinitions && !HasWorkstationName) {
                    continue;
                }
                std::sort(Station.Recipes.begin(), Station.Recipes.end(),
                    [](const g_Hack::AutoCraftRecipeView& Left, const g_Hack::AutoCraftRecipeView& Right) {
                        return Left.Name < Right.Name;
                    });
                NewCatalog.push_back(std::move(Station));
                if (NewCatalog.size() >= 128) break;
            }

            std::sort(NewCatalog.begin(), NewCatalog.end(),
                [](const g_Hack::AutoCraftStationView& Left, const g_Hack::AutoCraftStationView& Right) {
                    return Left.DistanceMeters < Right.DistanceMeters;
                });
            g_AutoCraftRuntime.Catalog = std::move(NewCatalog);

            // Keep the background view alive until actual inventory item
            // instances arrive. The crafting RPC needs their real ItemNetID;
            // default class definitions alone are insufficient.
            if (DiscoveryInventory) {
                bool RecipesReceived = false;
                for (const g_Hack::AutoCraftStationView& Station : g_AutoCraftRuntime.Catalog) {
                    if (Station.InventoryObjectIndex == DiscoveryInventory->Index) {
                        RecipesReceived = !Station.Recipes.empty();
                        break;
                    }
                }

                const bool TimedOut = Now - g_AutoCraftRuntime.DiscoveryRequestedAt >=
                    std::chrono::seconds(10);
                if (RecipesReceived || TimedOut) {
                    PC->ServerActorCloseRemoteInventory(DiscoveryInventory);
                    g_AutoCraftRuntime.DiscoveryInventory = nullptr;
                    g_AutoCraftRuntime.DiscoveryInventoryObjectIndex = -1;
                    g_AutoCraftRuntime.DiscoveryRequestedAt = Clock::time_point::min();
                    g_AutoCraftRuntime.NextDiscoveryRequest = Clock::time_point::min();
                    g_AutoCraftRuntime.Status = RecipesReceived ?
                        "工作站印痕/图纸已同步，请选择配方" :
                        "服务器未返回该工作站的印痕；请确认权限、电力，或手动打开一次后重选";
                }
                else {
                    const auto Waited = std::chrono::duration_cast<std::chrono::seconds>(
                        Now - g_AutoCraftRuntime.DiscoveryRequestedAt).count();
                    g_AutoCraftRuntime.Status = std::format(
                        "正在读取工作站印痕/图纸（{} / 10 秒）", std::max<int64_t>(Waited, 0));
                }
            }

            // Replication, range changes and catalog truncation are temporary.
            // Keep the selection; AutoCraft resolves its inventory identity and
            // recipe net ID again when the server sends replacement objects.
        }

        bool IsPumpShotgun(const SDK::AShooterWeapon* Weapon) {
            if (!IsUsableObject(Weapon) || !Weapon->Class) return false;
            std::string Identity = Weapon->GetFullName();
            Identity += Weapon->Class->GetName();
            if (SDK::UObject* Item = ReadReflectedObjectValue(
                const_cast<SDK::AShooterWeapon*>(Weapon), "AssociatedPrimalItem")) {
                Identity += Item->GetFullName();
                Identity += Item->Class->GetName();
            }
            Identity = NormalizeGachaItemName(std::move(Identity));
            return Identity.find("machinedshotgun") != std::string::npos ||
                Identity.find("pumpactionshotgun") != std::string::npos;
        }

        struct DlcTraitModifierPatch {
            SDK::UDataTable* Table = nullptr;
            int32_t TableObjectIndex = -1;
            float* ValueAddress = nullptr;
            float OriginalValue = 0.0f;
        };

        struct DlcSkillRuntimeState {
            g_Hack::DlcSkillDataView View;
            std::vector<DlcTraitModifierPatch> ProjectileDamagePatches;
            std::chrono::steady_clock::time_point NextScan = std::chrono::steady_clock::time_point::min();
        };

        DlcSkillRuntimeState g_DlcSkillRuntime;

        struct SkillModifierDiagnosticsRuntimeState {
            std::mutex Mutex;
            std::atomic<bool> Requested{ false };
            g_Hack::SkillModifierDiagnosticsView View;
        };

        SkillModifierDiagnosticsRuntimeState g_SkillModifierDiagnostics;

        using TastyBaitProcessEventFn = void(__fastcall*)(
            const SDK::UObject*, SDK::UFunction*, void*);

        struct TastyBaitOverrideRuntimeState {
            SDK::AShooterPlayerController* Controller = nullptr;
            int32_t ControllerObjectIndex = -1;
            SDK::UFunction* GetSkillModifierFunction = nullptr;
            TastyBaitProcessEventFn ControllerOriginalProcessEvent = nullptr;
            SDK::AShooterCharacter* Provider = nullptr;
            int32_t ProviderObjectIndex = -1;
            SDK::UFunction* GetProviderSkillModifierFunction = nullptr;
            SDK::UFunction* BPGetProviderSkillModifierFunction = nullptr;
            TastyBaitProcessEventFn ProviderOriginalProcessEvent = nullptr;
            std::atomic<bool> Enabled{ false };
            std::atomic<int32_t> CurrentRank{ 0 };
            std::atomic<float> Rank1Value{ 0.3f };
            std::atomic<float> Rank2Value{ 0.6f };
            std::atomic<uint64_t> ControllerQueryCalls{ 0 };
            std::atomic<uint64_t> ControllerQueryHits{ 0 };
            std::atomic<uint64_t> ProviderQueryCalls{ 0 };
            std::atomic<uint64_t> ProviderQueryHits{ 0 };
            std::mutex StatusMutex;
            std::string Status = "饵香四溢倍率覆盖未启用";
            std::chrono::steady_clock::time_point NextRefresh =
                std::chrono::steady_clock::time_point::min();
        };

        TastyBaitOverrideRuntimeState g_TastyBaitOverrideRuntime;

        void SetTastyBaitOverrideStatus(std::string Status) {
            std::lock_guard<std::mutex> Lock(g_TastyBaitOverrideRuntime.StatusMutex);
            g_TastyBaitOverrideRuntime.Status = std::move(Status);
        }

        void __fastcall HookedTastyBaitProcessEvent(const SDK::UObject* Context,
            SDK::UFunction* Function, void* Parameters) {
            g_Hook::CallbackScope Guard;
            TastyBaitProcessEventFn Original =
                g_TastyBaitOverrideRuntime.ControllerOriginalProcessEvent;
            if (!Original) return;

            Original(Context, Function, Parameters);
            if (!g_TastyBaitOverrideRuntime.Enabled.load(std::memory_order_acquire) ||
                Context != g_TastyBaitOverrideRuntime.Controller ||
                Function != g_TastyBaitOverrideRuntime.GetSkillModifierFunction ||
                !Parameters ||
                Function->Size != sizeof(SDK::Params::ShooterPlayerController_GetSkillModifier)) {
                return;
            }

            g_TastyBaitOverrideRuntime.ControllerQueryCalls.fetch_add(
                1, std::memory_order_relaxed);
            SDK::Params::ShooterPlayerController_GetSkillModifier* ModifierParameters =
                static_cast<SDK::Params::ShooterPlayerController_GetSkillModifier*>(Parameters);
            if (ModifierParameters->SkillName.ToString() != "TastyBait" ||
                ModifierParameters->ModifierName.ToString() != "FishingTimeReduction") {
                return;
            }

            const int32_t Rank = g_TastyBaitOverrideRuntime.CurrentRank.load(
                std::memory_order_relaxed);
            const float OverrideValue = Rank <= 1 ?
                g_TastyBaitOverrideRuntime.Rank1Value.load(std::memory_order_relaxed) :
                g_TastyBaitOverrideRuntime.Rank2Value.load(std::memory_order_relaxed);
            if (!std::isfinite(OverrideValue)) return;

            ModifierParameters->OutValue = std::clamp(OverrideValue, 0.0f, 0.95f);
            ModifierParameters->ReturnValue = true;
            g_TastyBaitOverrideRuntime.ControllerQueryHits.fetch_add(
                1, std::memory_order_relaxed);
        }

        void __fastcall HookedTastyBaitProviderProcessEvent(const SDK::UObject* Context,
            SDK::UFunction* Function, void* Parameters) {
            g_Hook::CallbackScope Guard;
            TastyBaitProcessEventFn Original =
                g_TastyBaitOverrideRuntime.ProviderOriginalProcessEvent;
            if (!Original) return;

            Original(Context, Function, Parameters);
            if (!g_TastyBaitOverrideRuntime.Enabled.load(std::memory_order_acquire) ||
                Context != g_TastyBaitOverrideRuntime.Provider || !Function || !Parameters) {
                return;
            }

            SDK::FName* SkillName = nullptr;
            SDK::FName* ModifierName = nullptr;
            float* OutModifier = nullptr;
            bool* ReturnValue = nullptr;
            const std::string FunctionName = Function->GetName();
            const bool IsNativeProviderQuery =
                Function == g_TastyBaitOverrideRuntime.GetProviderSkillModifierFunction ||
                EqualsNameIgnoreCase(FunctionName, "GetProviderSkillModifier");
            const bool IsBlueprintProviderQuery =
                Function == g_TastyBaitOverrideRuntime.BPGetProviderSkillModifierFunction ||
                EqualsNameIgnoreCase(FunctionName, "BPGetProviderSkillModifier");
            if (IsNativeProviderQuery &&
                Function->Size == sizeof(SDK::Params::SkillTreeProviderInterface_GetProviderSkillModifier)) {
                auto* ModifierParameters =
                    static_cast<SDK::Params::SkillTreeProviderInterface_GetProviderSkillModifier*>(Parameters);
                SkillName = &ModifierParameters->SkillName;
                ModifierName = &ModifierParameters->ModifierName;
                OutModifier = &ModifierParameters->OutModifier;
                ReturnValue = &ModifierParameters->ReturnValue;
            }
            else if (IsBlueprintProviderQuery &&
                Function->Size == sizeof(SDK::Params::SkillTreeProviderInterface_BPGetProviderSkillModifier)) {
                auto* ModifierParameters =
                    static_cast<SDK::Params::SkillTreeProviderInterface_BPGetProviderSkillModifier*>(Parameters);
                SkillName = &ModifierParameters->SkillName;
                ModifierName = &ModifierParameters->ModifierName;
                OutModifier = &ModifierParameters->OutModifier;
                ReturnValue = &ModifierParameters->ReturnValue;
            }
            else {
                return;
            }

            g_TastyBaitOverrideRuntime.ProviderQueryCalls.fetch_add(
                1, std::memory_order_relaxed);
            if (!SkillName || !ModifierName ||
                SkillName->ToString() != "TastyBait" ||
                ModifierName->ToString() != "FishingTimeReduction") {
                return;
            }

            const int32_t Rank = g_TastyBaitOverrideRuntime.CurrentRank.load(
                std::memory_order_relaxed);
            const float OverrideValue = Rank <= 1 ?
                g_TastyBaitOverrideRuntime.Rank1Value.load(std::memory_order_relaxed) :
                g_TastyBaitOverrideRuntime.Rank2Value.load(std::memory_order_relaxed);
            if (!OutModifier || !ReturnValue || !std::isfinite(OverrideValue)) return;

            *OutModifier = std::clamp(OverrideValue, 0.0f, 0.95f);
            *ReturnValue = true;
            g_TastyBaitOverrideRuntime.ProviderQueryHits.fetch_add(
                1, std::memory_order_relaxed);
        }

        void RemoveTastyBaitProcessEventHook() {
            g_TastyBaitOverrideRuntime.Enabled.store(false, std::memory_order_release);
            ezVMT::RemoveHook(reinterpret_cast<void*>(&HookedTastyBaitProcessEvent));
            ezVMT::RemoveHook(reinterpret_cast<void*>(&HookedTastyBaitProviderProcessEvent));
            g_TastyBaitOverrideRuntime.Controller = nullptr;
            g_TastyBaitOverrideRuntime.ControllerObjectIndex = -1;
            g_TastyBaitOverrideRuntime.GetSkillModifierFunction = nullptr;
            // Keep the native target available to a callback that entered just
            // before detachment; the DLL worker drains those calls before release.
            g_TastyBaitOverrideRuntime.Provider = nullptr;
            g_TastyBaitOverrideRuntime.ProviderObjectIndex = -1;
            g_TastyBaitOverrideRuntime.GetProviderSkillModifierFunction = nullptr;
            g_TastyBaitOverrideRuntime.BPGetProviderSkillModifierFunction = nullptr;
            // The original target remains valid after this hook is detached.
            g_TastyBaitOverrideRuntime.CurrentRank.store(0, std::memory_order_relaxed);
            g_TastyBaitOverrideRuntime.NextRefresh =
                std::chrono::steady_clock::time_point::min();
        }

        const char* GetTraitModifierStackingName(int StackingType) {
            switch (static_cast<SDK::EItemTraitModiferType>(StackingType)) {
            case SDK::EItemTraitModiferType::Multiplier: return "Multiplier";
            case SDK::EItemTraitModiferType::Additive: return "Additive";
            case SDK::EItemTraitModiferType::NoStacking: return "NoStacking";
            default: return "Unknown";
            }
        }

        bool IsBaitFragranceSkillCandidate(const std::string& InternalName,
            const std::string& DisplayName, const std::string& Description,
            const std::vector<g_Hack::SkillModifierView>& Modifiers) {
            if (DisplayName.find("饵香四溢") != std::string::npos ||
                Description.find("饵香四溢") != std::string::npos) {
                return true;
            }

            std::string Identity = InternalName;
            Identity += DisplayName;
            Identity += Description;
            Identity = NormalizeGachaItemName(std::move(Identity));
            const bool HasFishingToken = Identity.find("fishing") != std::string::npos ||
                Identity.find("fish") != std::string::npos ||
                Identity.find("bait") != std::string::npos ||
                Identity.find("lure") != std::string::npos ||
                Identity.find("bite") != std::string::npos ||
                Identity.find("hook") != std::string::npos ||
                Identity.find("attract") != std::string::npos;
            if (!HasFishingToken) return false;

            bool HasThirtyLikeValue = false;
            bool HasSixtyLikeValue = false;
            for (const g_Hack::SkillModifierView& Modifier : Modifiers) {
                const float AbsoluteValue = std::fabs(Modifier.ConfiguredValue);
                HasThirtyLikeValue = HasThirtyLikeValue ||
                    std::fabs(AbsoluteValue - 0.3f) < 0.001f ||
                    std::fabs(AbsoluteValue - 0.7f) < 0.001f ||
                    std::fabs(AbsoluteValue - 30.0f) < 0.01f;
                HasSixtyLikeValue = HasSixtyLikeValue ||
                    std::fabs(AbsoluteValue - 0.6f) < 0.001f ||
                    std::fabs(AbsoluteValue - 0.4f) < 0.001f ||
                    std::fabs(AbsoluteValue - 60.0f) < 0.01f;
            }
            return HasThirtyLikeValue && HasSixtyLikeValue;
        }

        void RestoreDlcTraitModifierPatches() {
            for (const DlcTraitModifierPatch& Patch : g_DlcSkillRuntime.ProjectileDamagePatches) {
                if (!Patch.Table || !Patch.ValueAddress || Patch.TableObjectIndex < 0) continue;
                if (!IsUsableObject(Patch.Table) || Patch.Table->Index != Patch.TableObjectIndex) continue;
                *Patch.ValueAddress = Patch.OriginalValue;
            }
            g_DlcSkillRuntime.ProjectileDamagePatches.clear();
            g_DlcSkillRuntime.View.DragontopiaModifiedValues = 0;
        }

        float GetSafeDragontopiaDamageMultiplier() {
            const float Value = g_Config::DragontopiaSkillDamageMultiplier;
            return std::isfinite(Value) ? std::clamp(Value, 0.5f, 5.0f) : 1.0f;
        }

        void ApplyDlcTraitModifierPatches() {
            const float Multiplier = GetSafeDragontopiaDamageMultiplier();
            int ModifiedCount = 0;
            for (const DlcTraitModifierPatch& Patch : g_DlcSkillRuntime.ProjectileDamagePatches) {
                if (!Patch.Table || !Patch.ValueAddress || Patch.TableObjectIndex < 0) continue;
                if (!IsUsableObject(Patch.Table) || Patch.Table->Index != Patch.TableObjectIndex) continue;

                const double Scaled = static_cast<double>(Patch.OriginalValue) * Multiplier;
                if (!std::isfinite(Scaled)) continue;
                *Patch.ValueAddress = static_cast<float>(std::clamp(Scaled, -100000.0, 100000.0));
                ++ModifiedCount;
            }
            g_DlcSkillRuntime.View.DragontopiaModifiedValues = ModifiedCount;
        }

        bool HasLocalSkillDataAuthority(SDK::UWorld* World) {
            if (SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World)) {
                return IsUsableObject(Human) && Human->HasAuthority();
            }

            if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
                return false;
            }
            SDK::ULocalPlayer* LocalPlayer = World->OwningGameInstance->LocalPlayers[0];
            return LocalPlayer && LocalPlayer->PlayerController && LocalPlayer->PlayerController->Pawn &&
                LocalPlayer->PlayerController->Pawn->HasAuthority();
        }

        std::string GetSkillTreeIdentity(SDK::UPrimalProgressionTreeAsset* Tree) {
            if (!Tree) return {};

            std::string Identity = Tree->GetFullName();
            Identity += Tree->SkillTreeTag.ToString();
            Identity += Tree->InputDataTable.ObjectID.AssetPath.PackageName.ToString();
            Identity += Tree->InputDataTable.ObjectID.AssetPath.AssetName.ToString();
            if (Tree->ProgressionGraph && IsUsableObject(Tree->ProgressionGraph)) {
                Identity += Tree->ProgressionGraph->GetFullName();
            }
            if (SDK::UClass* GraphClass = Tree->ProgressionGraphClass.Get()) {
                Identity += GraphClass->GetFullName();
            }
            return NormalizeGachaItemName(Identity);
        }

        SDK::UDataTable* ResolveLoadedSkillTable(
            SDK::UPrimalProgressionTreeAsset* Tree,
            const std::vector<SDK::UDataTable*>& LoadedTables) {
            if (!Tree) return nullptr;

            if (SDK::UDataTable* Direct = Tree->InputDataTable.Get()) {
                if (IsUsableObject(Direct)) return Direct;
            }

            const std::string AssetName = NormalizeGachaItemName(
                Tree->InputDataTable.ObjectID.AssetPath.AssetName.ToString());
            if (AssetName.empty() || AssetName == "none") return nullptr;

            for (SDK::UDataTable* Table : LoadedTables) {
                if (Table && NormalizeGachaItemName(Table->GetName()) == AssetName) return Table;
            }
            return nullptr;
        }

        void UpdateDlcSkillData(SDK::UWorld* World) {
            // The generated SDK is CL 826246 while the live game is CL 826342.
            // Walking loaded progression assets with the old hard-coded
            // UPrimalProgressionTreeAsset/UDataTable layouts caused an access
            // violation at RowStruct. Keep this path deliberately disabled
            // until those layouts are regenerated for the current game build.
            RestoreDlcTraitModifierPatches();
            g_DlcSkillRuntime.View = g_Hack::DlcSkillDataView{};
            g_DlcSkillRuntime.View.HasLocalAuthority = HasLocalSkillDataAuthority(World);
        }

        bool IsTekStrider(const SDK::UObject* Object) {
            if (!IsUsableObject(Object) || !Object->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return false;
            }
            const SDK::UClass* Type = Object->Class;
            while (Type) {
                if (NormalizeGachaItemName(Type->GetName()).find("tekstridercharacterbp") !=
                    std::string::npos) {
                    return true;
                }
                Type = static_cast<const SDK::UClass*>(Type->SuperStruct);
            }
            return false;
        }

        bool IsDaeodon(const SDK::UObject* Object) {
            if (!IsUsableObject(Object) || !Object->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return false;
            }
            const SDK::UClass* Type = Object->Class;
            while (Type) {
                const std::string TypeName = NormalizeGachaItemName(Type->GetName());
                if (TypeName.find("daeodoncharacterbp") != std::string::npos ||
                    TypeName.find("daeodoncharacter") != std::string::npos) {
                    return true;
                }
                Type = static_cast<const SDK::UClass*>(Type->SuperStruct);
            }
            return false;
        }

        void SetDaeodonHealingStatus(std::string Status) {
            std::scoped_lock Lock(g_DaeodonHealingRequest.Mutex);
            g_DaeodonHealingRequest.Status = std::move(Status);
        }

        bool HasTekStriderExcavationRig(const SDK::APrimalDinoCharacter* Dino) {
            if (!IsTekStrider(Dino) || !IsUsableObject(Dino->MyInventoryComponent)) {
                return false;
            }
            auto IsExcavationRig = [](const SDK::UPrimalItem* Item) {
                if (!IsUsableObject(Item) || !Item->Class) return false;
                const std::string Name = NormalizeGachaItemName(Item->Class->GetName());
                return Name.find("tekstriderharvester") != std::string::npos;
            };
            for (SDK::UPrimalItem* Item : Dino->MyInventoryComponent->EquippedItems) {
                if (IsExcavationRig(Item)) return true;
            }
            for (SDK::UPrimalItem* Item : Dino->MyInventoryComponent->InventoryItems) {
                if (IsExcavationRig(Item) && Item->bEquippedItem) return true;
            }
            return false;
        }

        bool ForcePastNumericProperty(SDK::UObject* Object, const char* PropertyName) {
            if (!IsUsableObject(Object) || !Object->Class || !PropertyName) return false;
            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0) return false;

            uint8_t* Address = reinterpret_cast<uint8_t*>(Object) + Property->Offset;
            const std::string TypeName = Property->ClassPrivate->Name.ToString();
            if (EqualsNameIgnoreCase(TypeName, "DoubleProperty") &&
                Property->ElementSize >= sizeof(double)) {
                *reinterpret_cast<double*>(Address) = -1000000000.0;
                return true;
            }
            if (EqualsNameIgnoreCase(TypeName, "FloatProperty") &&
                Property->ElementSize >= sizeof(float)) {
                *reinterpret_cast<float*>(Address) = -1000000000.0f;
                return true;
            }
            return false;
        }

    }

    std::string GetAutoFeedStatus() {
        return g_AutoFeedStatus;
    }

    void AutoFeed(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point NextScan = Clock::time_point::min();
        static std::unordered_map<int32_t, Clock::time_point> NextFeedByDino;
        static size_t RoundRobinOffset = 0;

        if (!g_Config::bAutoFeed) {
            g_AutoFeedStatus = "等待范围内恐龙";
            NextFeedByDino.clear();
            RoundRobinOffset = 0;
            return;
        }

        const Clock::time_point Now = Clock::now();
        if (Now < NextScan) return;
        const float FeedIntervalSeconds =
            std::clamp(g_Config::AutoFeedIntervalSeconds, 0.25f, 30.0f);
        const int64_t ScanIntervalMilliseconds = std::clamp<int64_t>(
            static_cast<int64_t>(FeedIntervalSeconds * 1000.0f), 100, 500);
        NextScan = Now + std::chrono::milliseconds(ScanIntervalMilliseconds);
        const Clock::duration FeedInterval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(FeedIntervalSeconds));

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        SDK::UClass* DinoClass = SDK::APrimalDinoCharacter::StaticClass();
        if (!World || !PC || !Human || !DinoClass) {
            g_AutoFeedStatus = "等待人物和世界数据";
            return;
        }

        int32_t PlayerTeam = 0;
        if (!ReadReflectedInt32Value(Human, "TargetingTeam", &PlayerTeam)) {
            g_AutoFeedStatus = "无法读取玩家部落信息";
            return;
        }

        SDK::TArray<SDK::AActor*> Actors;
        SDK::UGameplayStatics::GetAllActorsOfClass(World, DinoClass, &Actors);
        if (Actors.Num() < 0 || Actors.Num() > 100000) return;

        const float MaximumRange = std::clamp(g_Config::AutoFeedRangeMeters, 2.0f, 100.0f);
        std::vector<SDK::APrimalDinoCharacter*> Candidates;
        Candidates.reserve(static_cast<size_t>(Actors.Num()));
        int32_t ScannedDinos = 0;
        int32_t FriendlyDinos = 0;
        int32_t InRangeDinos = 0;
        int32_t MatchingDinos = 0;
        int32_t MatchingDaeodons = 0;
        float LowestDaeodonFood = std::numeric_limits<float>::infinity();

        for (int32_t Index = 0; Index < Actors.Num(); ++Index) {
            SDK::AActor* Actor = Actors[Index];
            if (!IsUsableObject(Actor) || !Actor->IsA(DinoClass)) continue;
            SDK::APrimalDinoCharacter* Dino = static_cast<SDK::APrimalDinoCharacter*>(Actor);
            if (Dino->IsDead()) continue;
            ++ScannedDinos;

            int32_t DinoTeam = 0;
            const bool HasDinoTeam = ReadReflectedInt32Value(Dino, "TargetingTeam", &DinoTeam);
            const bool IsSameTeam = HasDinoTeam && DinoTeam == PlayerTeam;
            const bool IsAlliedTeam = Dino->IsAlliedWithOtherTeam(PlayerTeam);
            if (!IsSameTeam && !IsAlliedTeam) continue;
            ++FriendlyDinos;

            const float Distance = Human->GetDistanceTo(Dino) * 0.01f;
            if (!std::isfinite(Distance) || Distance > MaximumRange) continue;
            ++InRangeDinos;

            std::string SearchIdentity = Dino->TamedName.ToString();
            SearchIdentity += " ";
            SearchIdentity += Dino->DescriptiveName.ToString();
            SearchIdentity += " ";
            SearchIdentity += Dino->Class ? Dino->Class->GetName() : "";
            if (!g_Util::IsEntityMatchMulti(SearchIdentity, g_Config::AutoFeedDinoFilter)) continue;
            // Only queue meat for dinosaurs whose native food type is carnivorous.
            if (!Dino->bIsCarnivore) continue;
            ++MatchingDinos;

            if (IsDaeodon(Dino) && IsUsableObject(Dino->MyCharacterStatusComponent)) {
                ++MatchingDaeodons;
                const float CurrentFood = Dino->MyCharacterStatusComponent->BPGetCurrentStatusValue(
                    SDK::EPrimalCharacterStatusValue::Food);
                if (std::isfinite(CurrentFood)) {
                    LowestDaeodonFood = std::min(LowestDaeodonFood, CurrentFood);
                }
            }

            if (!Dino->MyInventoryComponent) continue;
            const auto FeedIt = NextFeedByDino.find(Dino->Index);
            if (FeedIt != NextFeedByDino.end() && Now < FeedIt->second) continue;
            Candidates.push_back(Dino);
        }

        std::erase_if(NextFeedByDino,
            [Now](const auto& Pair) { return Pair.second + std::chrono::seconds(10) <= Now; });

        int32_t RequestsSent = 0;
        if (!Candidates.empty()) {
            RoundRobinOffset %= Candidates.size();
            constexpr int32_t MaxRequestsPerScan = 6;
            for (size_t Step = 0;
                Step < Candidates.size() && RequestsSent < MaxRequestsPerScan; ++Step) {
                SDK::APrimalDinoCharacter* Dino =
                    Candidates[(RoundRobinOffset + Step) % Candidates.size()];
                if (!g_Util::ProcessDinoFeed(PC, Dino)) continue;
                NextFeedByDino[Dino->Index] = Now + FeedInterval;
                ++RequestsSent;
            }
            RoundRobinOffset = (RoundRobinOffset + static_cast<size_t>(RequestsSent)) %
                Candidates.size();
        }

        std::string DaeodonFoodSuffix;
        if (MatchingDaeodons > 0 && std::isfinite(LowestDaeodonFood)) {
            DaeodonFoodSuffix = std::format(
                "；凶齿豨 {} 只，最低食物 {:.1f}{}",
                MatchingDaeodons, LowestDaeodonFood,
                LowestDaeodonFood < 100.0f ? "（低于治疗启动门槛）" : "");
        }

        if (RequestsSent > 0) {
            g_AutoFeedStatus = "已发送喂食请求 " + std::to_string(RequestsSent) +
                " 只；范围内匹配 " + std::to_string(MatchingDinos) + " 只";
            g_AutoFeedStatus += DaeodonFoodSuffix;
        }
        else if (!Candidates.empty()) {
            g_AutoFeedStatus = "已扫描到待喂恐龙，但没有找到可提交的肉类物品" +
                DaeodonFoodSuffix;
        }
        else {
            g_AutoFeedStatus = "扫描 " + std::to_string(ScannedDinos) +
                " 只 / 友方 " + std::to_string(FriendlyDinos) +
                " 只 / 范围内 " + std::to_string(InRangeDinos) +
                " 只 / 筛选匹配 " + std::to_string(MatchingDinos) + " 只";
            g_AutoFeedStatus += DaeodonFoodSuffix;
        }
    }

    void RequestEnableNearbyDaeodonHealing() {
        std::scoped_lock Lock(g_DaeodonHealingRequest.Mutex);
        g_DaeodonHealingRequest.Pending = true;
        g_DaeodonHealingRequest.Status = "已提交，等待游戏线程扫描附近凶齿豨";
    }

    std::string GetDaeodonHealingStatus() {
        std::scoped_lock Lock(g_DaeodonHealingRequest.Mutex);
        return g_DaeodonHealingRequest.Status;
    }

    void ProcessEnableNearbyDaeodonHealingRequest(SDK::UWorld* World) {
        {
            std::scoped_lock Lock(g_DaeodonHealingRequest.Mutex);
            if (!g_DaeodonHealingRequest.Pending) return;
            g_DaeodonHealingRequest.Pending = false;
        }

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        SDK::UClass* DinoClass = SDK::APrimalDinoCharacter::StaticClass();
        if (!World || !PC || !Human || !DinoClass) {
            SetDaeodonHealingStatus("等待人物和世界数据，请进入游戏后重试");
            return;
        }

        int32_t PlayerTeam = 0;
        if (!ReadReflectedInt32Value(Human, "TargetingTeam", &PlayerTeam)) {
            SetDaeodonHealingStatus("无法读取玩家部落信息");
            return;
        }

        SDK::TArray<SDK::AActor*> Actors;
        SDK::UGameplayStatics::GetAllActorsOfClass(World, DinoClass, &Actors);
        if (Actors.Num() < 0 || Actors.Num() > 100000) {
            SetDaeodonHealingStatus("附近恐龙列表尚未同步");
            return;
        }

        const float SearchRangeMeters = std::clamp(
            g_Config::AutoFeedRangeMeters, 2.0f, 100.0f);
        int32_t FriendlyDaeodons = 0;
        int32_t InSearchRange = 0;
        int32_t OutsideServerUseRange = 0;
        int32_t AlreadyEnabled = 0;
        int32_t EnableEntryNotFound = 0;
        int32_t RequestsSent = 0;
        constexpr int32_t MaxRequestsPerClick = 32;

        for (int32_t ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex) {
            SDK::AActor* Actor = Actors[ActorIndex];
            if (!IsDaeodon(Actor)) continue;
            auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(Actor);
            if (Dino->IsDead()) continue;

            int32_t DinoTeam = 0;
            const bool HasDinoTeam = ReadReflectedInt32Value(Dino, "TargetingTeam", &DinoTeam);
            const bool IsSameTeam = HasDinoTeam && DinoTeam == PlayerTeam;
            const bool IsAlliedTeam = Dino->IsAlliedWithOtherTeam(PlayerTeam);
            if (!IsSameTeam && !IsAlliedTeam) continue;
            ++FriendlyDaeodons;

            const float DistanceMeters = Human->GetDistanceTo(Dino) * 0.01f;
            if (!std::isfinite(DistanceMeters) || DistanceMeters > SearchRangeMeters) continue;
            ++InSearchRange;

            // ServerMultiUse is the same server-authoritative path as selecting
            // an option from the creature radial wheel. Dedicated servers still
            // validate interaction distance, so do not send requests that are
            // guaranteed to be discarded.
            const float OverrideRangeUnits = Dino->BPOverrideServerMultiUseAcceptRange();
            const float ServerUseRangeMeters =
                std::isfinite(OverrideRangeUnits) && OverrideRangeUnits > 0.0f
                ? std::clamp(OverrideRangeUnits * 0.01f, 1.0f, 25.0f)
                : 6.0f;
            if (DistanceMeters > ServerUseRangeMeters) {
                ++OutsideServerUseRange;
                continue;
            }

            SDK::TArray<SDK::FMultiUseEntry> Entries;
            Dino->GetMultiUseEntries(PC, &Entries, -1);
            if (Entries.Num() < 0 || Entries.Num() > 512) {
                ++EnableEntryNotFound;
                continue;
            }

            SDK::UObject* EnableUseObject = nullptr;
            int32_t EnableUseIndex = -1;
            bool EnableClientSideOnly = false;
            bool FoundDisableEntry = false;

            for (int32_t EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex) {
                const SDK::FMultiUseEntry& Entry = Entries[EntryIndex];
                const std::string RawText = Entry.UseString.ToString();
                const std::string NormalizedText = NormalizeGachaItemName(RawText);

                const bool EnglishPassiveHealing =
                    NormalizedText.find("passivehealing") != std::string::npos ||
                    (NormalizedText.find("passive") != std::string::npos &&
                        (NormalizedText.find("heal") != std::string::npos ||
                            NormalizedText.find("regen") != std::string::npos));
                const bool ChinesePassiveHealing =
                    RawText.find("自动治疗") != std::string::npos ||
                    ((RawText.find("被动") != std::string::npos ||
                        RawText.find("自动") != std::string::npos) &&
                        (RawText.find("治疗") != std::string::npos ||
                            RawText.find("治愈") != std::string::npos ||
                            RawText.find("回血") != std::string::npos ||
                            RawText.find("恢复") != std::string::npos));
                if (!EnglishPassiveHealing && !ChinesePassiveHealing) continue;

                const bool IsDisableEntry =
                    NormalizedText.find("disable") != std::string::npos ||
                    NormalizedText.find("deactivate") != std::string::npos ||
                    NormalizedText.find("turnoff") != std::string::npos ||
                    NormalizedText.find("stop") != std::string::npos ||
                    RawText.find("禁用") != std::string::npos ||
                    RawText.find("关闭") != std::string::npos ||
                    RawText.find("停用") != std::string::npos ||
                    RawText.find("停止") != std::string::npos;
                if (IsDisableEntry) {
                    FoundDisableEntry = true;
                    continue;
                }

                const bool IsEnableEntry =
                    NormalizedText.find("enable") != std::string::npos ||
                    NormalizedText.find("activate") != std::string::npos ||
                    NormalizedText.find("turnon") != std::string::npos ||
                    NormalizedText.find("start") != std::string::npos ||
                    RawText.find("启用") != std::string::npos ||
                    RawText.find("开启") != std::string::npos ||
                    RawText.find("打开") != std::string::npos ||
                    RawText.find("启动") != std::string::npos;
                if (!IsEnableEntry) continue;

                EnableUseObject = IsUsableObject(Entry.ForComponent)
                    ? static_cast<SDK::UObject*>(Entry.ForComponent)
                    : static_cast<SDK::UObject*>(Dino);
                EnableUseIndex = Entry.UseIndex;
                EnableClientSideOnly = Entry.bClientSideOnly;
                break;
            }

            if (!EnableUseObject || EnableUseIndex < 0) {
                if (FoundDisableEntry) ++AlreadyEnabled;
                else ++EnableEntryNotFound;
                continue;
            }
            if (RequestsSent >= MaxRequestsPerClick) continue;

            if (EnableClientSideOnly) {
                PC->ClientDoMultiUse(EnableUseObject, EnableUseIndex);
            }
            else {
                PC->ServerMultiUse(EnableUseObject, EnableUseIndex);
            }
            ++RequestsSent;
        }

        if (FriendlyDaeodons == 0) {
            SetDaeodonHealingStatus("没有检测到己方或友方凶齿豨");
            return;
        }
        if (InSearchRange == 0) {
            SetDaeodonHealingStatus(std::format(
                "检测到 {} 只友方凶齿豨，但都不在 {:.0f}m 搜索范围内",
                FriendlyDaeodons, SearchRangeMeters));
            return;
        }

        SetDaeodonHealingStatus(std::format(
            "已提交开启 {} 只 / 已开启 {} 只 / 超出专服交互距离 {} 只 / 未识别轮盘项 {} 只",
            RequestsSent, AlreadyEnabled, OutsideServerUseRange, EnableEntryNotFound));
    }

    void AutoEatDrink(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point NextCheck = Clock::time_point::min();
        static Clock::time_point NextUse = Clock::time_point::min();

        if (!g_Config::bAutoEatDrink) {
            g_AutoEatDrinkStatus = "自动吃喝未启用";
            NextCheck = Clock::time_point::min();
            return;
        }

        const Clock::time_point Now = Clock::now();
        if (NextCheck != Clock::time_point::min() && Now < NextCheck) return;
        NextCheck = Now + std::chrono::milliseconds(250);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalConsumableCharacter(World);
        if (!PC || !Human || Human->IsDead() || !Human->MyInventoryComponent ||
            !Human->MyCharacterStatusComponent) {
            g_AutoEatDrinkStatus = "等待人物、背包和状态数据";
            return;
        }

        SDK::UPrimalCharacterStatusComponent* Status = Human->MyCharacterStatusComponent;
        float FoodRatio = Status->BPGetPercentStatusValue(SDK::EPrimalCharacterStatusValue::Food);
        float WaterRatio = Status->BPGetPercentStatusValue(SDK::EPrimalCharacterStatusValue::Water);
        if (!std::isfinite(FoodRatio) || !std::isfinite(WaterRatio)) {
            g_AutoEatDrinkStatus = "人物饥饿/水分数据无效";
            return;
        }
        if (FoodRatio > 1.5f) FoodRatio /= 100.0f;
        if (WaterRatio > 1.5f) WaterRatio /= 100.0f;
        FoodRatio = std::clamp(FoodRatio, 0.0f, 1.0f);
        WaterRatio = std::clamp(WaterRatio, 0.0f, 1.0f);

        const float Threshold = std::clamp(
            g_Config::AutoEatDrinkThresholdPercent, 10.0f, 90.0f) / 100.0f;
        const bool NeedsFood = FoodRatio <= Threshold;
        const bool NeedsWater = WaterRatio <= Threshold;
        if (!NeedsFood && !NeedsWater) {
            g_AutoEatDrinkStatus = std::format("食物 {:.0f}% / 水分 {:.0f}%",
                FoodRatio * 100.0f, WaterRatio * 100.0f);
            return;
        }

        if (NextUse != Clock::time_point::min() && Now < NextUse) return;

        SDK::UPrimalInventoryComponent* Inventory = Human->MyInventoryComponent;
        SDK::EPrimalCharacterStatusValue Target = FoodRatio <= WaterRatio ?
            SDK::EPrimalCharacterStatusValue::Food : SDK::EPrimalCharacterStatusValue::Water;
        if ((Target == SDK::EPrimalCharacterStatusValue::Food && !NeedsFood) ||
            (Target == SDK::EPrimalCharacterStatusValue::Water && !NeedsWater)) {
            Target = NeedsFood ? SDK::EPrimalCharacterStatusValue::Food :
                SDK::EPrimalCharacterStatusValue::Water;
        }

        SDK::UPrimalItem* Item = FindBestAutoConsumable(Inventory, Target);
        if (!Item && NeedsFood && NeedsWater) {
            Target = Target == SDK::EPrimalCharacterStatusValue::Food ?
                SDK::EPrimalCharacterStatusValue::Water : SDK::EPrimalCharacterStatusValue::Food;
            Item = FindBestAutoConsumable(Inventory, Target);
        }

        if (!Item) {
            g_AutoEatDrinkStatus = std::format("食物 {:.0f}% / 水分 {:.0f}%：没有安全的熟食、浆果或装水容器",
                FoodRatio * 100.0f, WaterRatio * 100.0f);
            NextUse = Now + std::chrono::seconds(1);
            return;
        }

        const std::string ItemName = Item->DescriptiveNameBase ?
            Item->DescriptiveNameBase.ToString() : Item->Class->GetName();
        PC->ServerRequestInventoryUseItem(Inventory, Item->ItemID, Item->SlotIndex);
        g_AutoEatDrinkStatus = std::format("已使用 {} 补充{}（食物 {:.0f}% / 水分 {:.0f}%）",
            ItemName,
            Target == SDK::EPrimalCharacterStatusValue::Food ? "食物" : "水分",
            FoodRatio * 100.0f, WaterRatio * 100.0f);
        NextUse = Now + std::chrono::milliseconds(1250);
    }

    std::string GetAutoEatDrinkStatus() {
        return g_AutoEatDrinkStatus;
    }

    void AutoUseBrews(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point NextCheck = Clock::time_point::min();
        static Clock::time_point NextMedicalUse = Clock::time_point::min();
        static Clock::time_point NextEnergyUse = Clock::time_point::min();

        if (!g_Config::bAutoMedicalBrew && !g_Config::bAutoEnergyBrew) {
            g_AutoBrewStatus = "自动药酒未启用";
            NextCheck = Clock::time_point::min();
            return;
        }

        const Clock::time_point Now = Clock::now();
        if (NextCheck != Clock::time_point::min() && Now < NextCheck) return;
        NextCheck = Now + std::chrono::milliseconds(250);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalConsumableCharacter(World);
        if (!PC || !Human || Human->IsDead() || !Human->MyInventoryComponent ||
            !Human->MyCharacterStatusComponent) {
            g_AutoBrewStatus = "等待人物、背包和状态数据";
            return;
        }

        auto NormalizeStatusRatio = [](float Value) {
            if (!std::isfinite(Value)) return -1.0f;
            if (Value > 1.5f) Value /= 100.0f;
            return std::clamp(Value, 0.0f, 1.0f);
        };
        SDK::UPrimalCharacterStatusComponent* Status = Human->MyCharacterStatusComponent;
        const float HealthRatio = NormalizeStatusRatio(
            Status->BPGetPercentStatusValue(SDK::EPrimalCharacterStatusValue::Health));
        const float StaminaRatio = NormalizeStatusRatio(
            Status->BPGetPercentStatusValue(SDK::EPrimalCharacterStatusValue::Stamina));
        if (HealthRatio < 0.0f || StaminaRatio < 0.0f) {
            g_AutoBrewStatus = "人物血量/耐力数据无效";
            return;
        }

        const float HealthThreshold = std::clamp(
            g_Config::MedicalBrewThresholdPercent, 5.0f, 95.0f) / 100.0f;
        const float StaminaThreshold = std::clamp(
            g_Config::EnergyBrewThresholdPercent, 5.0f, 95.0f) / 100.0f;
        SDK::UPrimalInventoryComponent* Inventory = Human->MyInventoryComponent;

        if (g_Config::bAutoMedicalBrew && HealthRatio <= HealthThreshold &&
            (NextMedicalUse == Clock::time_point::min() || Now >= NextMedicalUse)) {
            if (SDK::UPrimalItem* Brew = FindRequestedBrew(
                Inventory, SDK::EPrimalCharacterStatusValue::Health)) {
                const std::string Name = Brew->DescriptiveNameBase ?
                    Brew->DescriptiveNameBase.ToString() : Brew->Class->GetName();
                PC->ServerRequestInventoryUseItem(Inventory, Brew->ItemID, Brew->SlotIndex);
                NextMedicalUse = Now + std::chrono::milliseconds(1250);
                g_AutoBrewStatus = std::format("血量 {:.0f}%：已使用 {}",
                    HealthRatio * 100.0f, Name);
                return;
            }
        }

        if (g_Config::bAutoEnergyBrew && StaminaRatio <= StaminaThreshold &&
            (NextEnergyUse == Clock::time_point::min() || Now >= NextEnergyUse)) {
            if (SDK::UPrimalItem* Brew = FindRequestedBrew(
                Inventory, SDK::EPrimalCharacterStatusValue::Stamina)) {
                const std::string Name = Brew->DescriptiveNameBase ?
                    Brew->DescriptiveNameBase.ToString() : Brew->Class->GetName();
                PC->ServerRequestInventoryUseItem(Inventory, Brew->ItemID, Brew->SlotIndex);
                NextEnergyUse = Now + std::chrono::milliseconds(1250);
                g_AutoBrewStatus = std::format("耐力 {:.0f}%：已使用 {}",
                    StaminaRatio * 100.0f, Name);
                return;
            }
        }

        const bool MissingMedical = g_Config::bAutoMedicalBrew &&
            HealthRatio <= HealthThreshold &&
            !FindRequestedBrew(Inventory, SDK::EPrimalCharacterStatusValue::Health);
        const bool MissingEnergy = g_Config::bAutoEnergyBrew &&
            StaminaRatio <= StaminaThreshold &&
            !FindRequestedBrew(Inventory, SDK::EPrimalCharacterStatusValue::Stamina);
        if (MissingMedical || MissingEnergy) {
            g_AutoBrewStatus = MissingMedical && MissingEnergy ?
                "已低于阈值，但背包没有医疗酿酒和能量酿酒" :
                (MissingMedical ? "已低于血量阈值，但背包没有医疗酿酒" :
                    "已低于耐力阈值，但背包没有能量酿酒");
            return;
        }

        g_AutoBrewStatus = std::format("血量 {:.0f}% / 耐力 {:.0f}%",
            HealthRatio * 100.0f, StaminaRatio * 100.0f);
    }

    std::string GetAutoBrewStatus() {
        return g_AutoBrewStatus;
    }

    // ================= 自动汤类增益（buff 到期自动续） =================
    namespace {
        std::string g_AutoBuffSoupStatus = "自动汤类增益未启用";
        std::string g_FireExtinguishStatus = "自动灭火未启用";

        // 归一化类名链：沿继承链拼接类名，小写并去掉非字母数字。
        std::string NormalizedClassChain(const SDK::UObject* Object) {
            std::string Out;
            if (!Object || !Object->Class) return Out;
            for (auto* C = Object->Class; C; C = static_cast<SDK::UClass*>(C->SuperStruct)) {
                Out += NormalizeChain(C->GetName());
                Out += ' ';
            }
            Out += NormalizeChain(Object->GetName());
            return Out;
        }

        bool SoupKindEnabled(SoupKind Kind) {
            switch (Kind) {
                case SoupKind::BattleTartare: return g_Config::bSoupBattleTartare;
                case SoupKind::CactusBroth: return g_Config::bSoupCactusBroth;
                case SoupKind::FocalChili: return g_Config::bSoupFocalChili;
                case SoupKind::FriaCurry: return g_Config::bSoupFriaCurry;
                case SoupKind::CalienSoup: return g_Config::bSoupCalienSoup;
                case SoupKind::LazarusChowder: return g_Config::bSoupLazarusChowder;
                case SoupKind::EnduroStew: return g_Config::bSoupEnduroStew;
                case SoupKind::ShadowSteak: return g_Config::bSoupShadowSteak;
                case SoupKind::MushroomBrew: return g_Config::bSoupMushroomBrew;
                default: return false;
            }
        }

        SDK::UPrimalItem* FindBuffingSoupItem(SDK::UPrimalInventoryComponent* Inventory, SoupKind Kind) {
            if (!IsUsableObject(Inventory)) return nullptr;
            const auto Tokens = SoupItemTokens(Kind);
            for (int32_t Index = 0; Index < Inventory->InventoryItems.Num(); ++Index) {
                SDK::UPrimalItem* Item = Inventory->InventoryItems[Index];
                if (!IsUsableObject(Item) || Item->ItemQuantity <= 0 ||
                    Item->bIsBlueprint || Item->bIsEngram || Item->bIsItemSkin) continue;
                const std::string Chain = NormalizedClassChain(Item);
                if (!IsBuffingSoupChain(Chain) || !ChainContainsAnyToken(Chain, Tokens)) continue;
                return Item;
            }
            return nullptr;
        }

        bool HumanHasSoupBuff(SDK::AShooterCharacter* Human, SoupKind Kind) {
            SDK::TArray<SDK::APrimalBuff*> Buffs{};
            Human->GetAllBuffs(&Buffs);
            if (Buffs.Num() < 0 || Buffs.Num() > 4096) return false;
            const auto Tokens = SoupBuffTokens(Kind);
            for (int32_t Index = 0; Index < Buffs.Num(); ++Index) {
                SDK::APrimalBuff* Buff = Buffs[Index];
                if (!IsUsableObject(Buff)) continue;
                if (ChainContainsAnyToken(NormalizedClassChain(Buff), Tokens)) return true;
            }
            return false;
        }

        bool HumanIsBurning(SDK::AShooterCharacter* Human) {
            SDK::TArray<SDK::APrimalBuff*> Buffs{};
            Human->GetAllBuffs(&Buffs);
            if (Buffs.Num() < 0 || Buffs.Num() > 4096) return false;
            for (int32_t Index = 0; Index < Buffs.Num(); ++Index) {
                SDK::APrimalBuff* Buff = Buffs[Index];
                if (!IsUsableObject(Buff)) continue;
                if (IsBurningBuffChain(NormalizedClassChain(Buff))) return true;
            }
            return false;
        }

        SDK::UPrimalItem* FindAntifireGrenade(SDK::UPrimalInventoryComponent* Inventory) {
            if (!IsUsableObject(Inventory)) return nullptr;
            for (int32_t Index = 0; Index < Inventory->InventoryItems.Num(); ++Index) {
                SDK::UPrimalItem* Item = Inventory->InventoryItems[Index];
                if (!IsUsableObject(Item) || Item->ItemQuantity <= 0 ||
                    Item->bIsBlueprint || Item->bIsEngram || Item->bIsItemSkin) continue;
                if (IsAntifireGrenadeChain(NormalizedClassChain(Item))) return Item;
            }
            return nullptr;
        }

        std::string JoinNames(const std::vector<std::string>& Names) {
            std::string Out;
            for (size_t I = 0; I < Names.size(); ++I) {
                if (I) Out += "、";
                Out += Names[I];
            }
            return Out;
        }
    }

    void AutoBuffSoup(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point NextCheck = Clock::time_point::min();
        static Clock::time_point NextUse[static_cast<int>(SoupKind::Count)]{};

        if (!g_Config::bAutoBuffSoup) {
            g_AutoBuffSoupStatus = "自动汤类增益未启用";
            NextCheck = Clock::time_point::min();
            return;
        }
        const Clock::time_point Now = Clock::now();
        if (NextCheck != Clock::time_point::min() && Now < NextCheck) return;
        NextCheck = Now + std::chrono::milliseconds(500);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!PC || !Human || Human->IsDead() || !IsUsableObject(Human->MyInventoryComponent)) {
            g_AutoBuffSoupStatus = "等待人物与背包数据";
            return;
        }
        auto* Inventory = Human->MyInventoryComponent;

        std::vector<std::string> Missing;
        for (int K = 0; K < static_cast<int>(SoupKind::Count); ++K) {
            const auto Kind = static_cast<SoupKind>(K);
            if (!SoupKindEnabled(Kind)) continue;
            if (HumanHasSoupBuff(Human, Kind)) continue;
            if (NextUse[K] != Clock::time_point::min() && Now < NextUse[K]) continue;

            SDK::UPrimalItem* Soup = FindBuffingSoupItem(Inventory, Kind);
            if (!Soup) {
                Missing.emplace_back(SoupDisplayName(Kind));
                continue;
            }
            const std::string Name = Soup->DescriptiveNameBase ?
                Soup->DescriptiveNameBase.ToString() : Soup->Class->GetName();
            PC->ServerRequestInventoryUseItem(Inventory, Soup->ItemID, Soup->SlotIndex);
            NextUse[K] = Now + std::chrono::milliseconds(1500);
            g_AutoBuffSoupStatus = std::format("已使用 {} 续增益", Name);
            return;
        }
        g_AutoBuffSoupStatus = Missing.empty() ? "所选汤类增益均生效中" :
            std::format("缺少汤类：{}", JoinNames(Missing));
    }

    std::string GetAutoBuffSoupStatus() {
        return g_AutoBuffSoupStatus;
    }

    // ================= 身体着火自动投掷灭火手雷 =================
    namespace {
        enum class ExtinguishStage { Idle, WaitEquip, WaitAim, WaitThrow, WaitRestore };
    }

    void AutoFireExtinguish(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct FireExtinguishState {
            ExtinguishStage Stage = ExtinguishStage::Idle;
            Clock::time_point NextCheck = Clock::time_point::min();
            Clock::time_point NextAction = Clock::time_point::min();
            Clock::time_point NextGrenade = Clock::time_point::min();
            SDK::FItemNetID GrenadeID{};
            SDK::FItemNetID PrevWeaponID{};
            SDK::UPrimalItem* GrenadeItem = nullptr; // 免举起动画的临时补丁对象
            bool HasPrevWeapon = false;
            bool AimDown = false;
            SDK::FRotator SavedRotation{};
            SDK::UWorld* World = nullptr;
        };
        static FireExtinguishState S;

        // 恢复物品上的“跳过装备动画”临时补丁（bSkipEquipAnimation）。
        auto RestoreGrenadeAnimPatch = [&]() {
            if (S.GrenadeItem && IsUsableObject(S.GrenadeItem)) S.GrenadeItem->bSkipEquipAnimation = false;
            S.GrenadeItem = nullptr;
        };
        auto RestoreAim = [&](SDK::AShooterPlayerController* PC) {
            if (S.AimDown && PC) PC->SetControlRotation(S.SavedRotation);
            S.AimDown = false;
        };
        auto CancelToIdle = [&](SDK::AShooterPlayerController* PC, const char* Status, Clock::time_point NextGrenadeAt) {
            RestoreAim(PC);
            RestoreGrenadeAnimPatch();
            S = FireExtinguishState{};
            S.World = World;
            S.NextGrenade = NextGrenadeAt;
            g_FireExtinguishStatus = Status;
        };

        if (!g_Config::bAutoFireExtinguish) {
            auto* PC0 = GetLocalShooterController(World);
            CancelToIdle(PC0, "自动灭火未启用", Clock::time_point::min());
            return;
        }
        if (S.World != World) {
            auto* PC0 = GetLocalShooterController(World);
            CancelToIdle(PC0, "世界已切换，自动灭火就绪", Clock::time_point::min());
            return;
        }

        const Clock::time_point Now = Clock::now();
        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
        const bool Ready = PC && Human && !Human->IsDead() &&
            IsUsableObject(Human->MyInventoryComponent) && !g_MDX12::g_MenuState::g_isOpen &&
            !PC->bShowMouseCursor && !PC->IsViewingInventoryUI() && !IsFreeCameraActive() &&
            (!GameWindow || GetForegroundWindow() == GameWindow);
        if (!Ready) {
            if (S.Stage != ExtinguishStage::Idle)
                CancelToIdle(PC, "界面或状态变化，本次灭火已取消", S.NextGrenade);
            else if (S.NextCheck != Clock::time_point::min() && Now < S.NextCheck) return;
            else {
                S.NextCheck = Now + std::chrono::milliseconds(500);
                g_FireExtinguishStatus = !PC || !Human ? "等待人物与游戏前台" :
                    Human->IsDead() ? "人物死亡，自动灭火暂停" : "菜单、背包或游戏未在前台，自动灭火暂停";
            }
            return;
        }

        const float IntervalSeconds = std::clamp(g_Config::FireExtinguishIntervalSeconds, 2.0f, 60.0f);

        switch (S.Stage) {
            case ExtinguishStage::Idle: {
                if (S.NextCheck != Clock::time_point::min() && Now < S.NextCheck) return;
                S.NextCheck = Now + std::chrono::milliseconds(250);
                if (!HumanIsBurning(Human)) {
                    g_FireExtinguishStatus = "未检测到身体燃烧";
                    return;
                }
                if (S.NextGrenade != Clock::time_point::min() && Now < S.NextGrenade) {
                    g_FireExtinguishStatus = "检测到燃烧，等待灭火手雷冷却";
                    return;
                }
                auto* Inventory = Human->MyInventoryComponent;
                SDK::UPrimalItem* Grenade = FindAntifireGrenade(Inventory);
                if (!Grenade) {
                    g_FireExtinguishStatus = "检测到燃烧，但背包没有灭火手雷";
                    S.NextGrenade = Now + std::chrono::seconds(3);
                    return;
                }
                if (IsUsableObject(Human->CurrentWeapon) &&
                    IsUsableObject(Human->CurrentWeapon->AssociatedPrimalItem)) {
                    S.PrevWeaponID = Human->CurrentWeapon->AssociatedPrimalItem->ItemID;
                    S.HasPrevWeapon = true;
                }
                S.GrenadeID = Grenade->ItemID;
                // 跳过装备时的举起动画，让灭火手雷“秒装备→立即投掷”。
                Grenade->bSkipEquipAnimation = true;
                S.GrenadeItem = Grenade;
                PC->ServerEquipPawnItem(Grenade->ItemID);
                S.Stage = ExtinguishStage::WaitEquip;
                S.NextAction = Now + std::chrono::milliseconds(1500);
                g_FireExtinguishStatus = "已装备灭火手雷，准备在脚下投掷";
                return;
            }
            case ExtinguishStage::WaitEquip: {
                // 武器实例就绪即投掷：按物品 ID 或类名链（含模组变体）双保险。
                const bool Equipped = [&]() {
                    auto* W = Human->CurrentWeapon;
                    if (!IsUsableObject(W)) return false;
                    if (IsUsableObject(W->AssociatedPrimalItem) &&
                        W->AssociatedPrimalItem->ItemID.ItemID1 == S.GrenadeID.ItemID1 &&
                        W->AssociatedPrimalItem->ItemID.ItemID2 == S.GrenadeID.ItemID2) return true;
                    return IsAntifireGrenadeChain(NormalizedClassChain(W));
                }();
                if (Equipped) {
                    S.SavedRotation = PC->GetControlRotation();
                    SDK::FRotator Down = S.SavedRotation;
                    Down.Pitch = -78.0f;
                    Down.Roll = 0.0f;
                    PC->SetControlRotation(Down);
                    S.AimDown = true;
                    S.Stage = ExtinguishStage::WaitAim;
                    S.NextAction = Now + std::chrono::milliseconds(200);
                    g_FireExtinguishStatus = "已压低视角，准备向脚下投掷";
                }
                else if (Now >= S.NextAction) {
                    CancelToIdle(PC, "装备灭火手雷超时，本次灭火已取消",
                        Now + std::chrono::seconds(3));
                }
                return;
            }
            case ExtinguishStage::WaitAim: {
                if (Now < S.NextAction) return;
                auto* Weapon = Human->CurrentWeapon;
                if (!IsUsableObject(Weapon) ||
                    !Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass())) {
                    CancelToIdle(PC, "未取得投射物武器，本次灭火已取消",
                        Now + std::chrono::seconds(3));
                    return;
                }
                auto* ProjectileWeapon = static_cast<SDK::AShooterWeapon_Projectile*>(Weapon);
                SDK::FVector Origin{}, ShootDir{};
                ProjectileWeapon->BPGetProjectileSpawnTransform(&Origin, &ShootDir);
                SDK::FVector_NetQuantizeNormal Dir{};
                Dir.X = ShootDir.X; Dir.Y = ShootDir.Y; Dir.Z = ShootDir.Z;
                const int32_t ProjectileID = static_cast<int32_t>(GetTickCount64() & 0x7FFFFFFF);
                ProjectileWeapon->ServerFireProjectile(Origin, Dir, ProjectileID);
                RestoreGrenadeAnimPatch(); // 装备动画已过，恢复物品原值
                S.Stage = ExtinguishStage::WaitThrow;
                S.NextAction = Now + std::chrono::milliseconds(400);
                g_FireExtinguishStatus = "已向脚下投掷灭火手雷";
                return;
            }
            case ExtinguishStage::WaitThrow: {
                if (Now < S.NextAction) return;
                RestoreAim(PC);
                if (S.HasPrevWeapon) {
                    PC->ServerEquipPawnItem(S.PrevWeaponID);
                    S.Stage = ExtinguishStage::WaitRestore;
                    S.NextAction = Now + std::chrono::milliseconds(1500);
                    g_FireExtinguishStatus = "灭火完成，正在恢复原武器";
                }
                else {
                    S = FireExtinguishState{};
                    S.World = World;
                    S.NextGrenade = Now + std::chrono::milliseconds(
                        static_cast<int64_t>(IntervalSeconds * 1000.0f));
                    g_FireExtinguishStatus = "灭火完成";
                }
                return;
            }
            case ExtinguishStage::WaitRestore: {
                const bool Restored = !S.HasPrevWeapon ||
                    (IsUsableObject(Human->CurrentWeapon) &&
                        IsUsableObject(Human->CurrentWeapon->AssociatedPrimalItem) &&
                        Human->CurrentWeapon->AssociatedPrimalItem->ItemID.ItemID1 == S.PrevWeaponID.ItemID1 &&
                        Human->CurrentWeapon->AssociatedPrimalItem->ItemID.ItemID2 == S.PrevWeaponID.ItemID2);
                if (Restored || Now >= S.NextAction) {
                    S = FireExtinguishState{};
                    S.World = World;
                    S.NextGrenade = Now + std::chrono::milliseconds(
                        static_cast<int64_t>(IntervalSeconds * 1000.0f));
                    g_FireExtinguishStatus = Restored ? "灭火完成，已恢复原武器" :
                        "恢复原武器超时，进入灭火冷却";
                }
                return;
            }
        }
    }

    std::string GetFireExtinguishStatus() {
        return g_FireExtinguishStatus;
    }

    void AutoPickupDroppedItems(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct AutoPickupRuntimeState {
            SDK::UWorld* World = nullptr;
            Clock::time_point NextScan = Clock::time_point::min();
            std::unordered_map<uint64_t, Clock::time_point> RetryAfter;
        };
        static AutoPickupRuntimeState State;

        if (!g_Config::bAutoPickupDroppedItems) {
            State = AutoPickupRuntimeState{};
            SetAutoPickupStatus("自动拾取未启用");
            return;
        }
        if (State.World != World) {
            State = AutoPickupRuntimeState{};
            State.World = World;
        }

        const Clock::time_point Now = Clock::now();
        if (State.NextScan != Clock::time_point::min() && Now < State.NextScan) return;
        const int64_t IntervalMilliseconds = std::clamp<int64_t>(
            static_cast<int64_t>(g_Config::AutoPickupIntervalMs), 150, 2000);
        State.NextScan = Now + std::chrono::milliseconds(IntervalMilliseconds);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!World || !IsUsableObject(PC) || !IsUsableObject(Human) || Human->IsDead() ||
            !IsUsableObject(Human->MyInventoryComponent) ||
            !IsUsableObject(Human->MyCharacterStatusComponent) || !IsUsableObject(PC->Pawn)) {
            SetAutoPickupStatus("等待人物、状态与世界数据");
            return;
        }

        // Picking up while mounted is ambiguous because the server may route
        // weight and ownership to a different inventory. Restrict this helper
        // to a locally controlled human on foot.
        if (!PC->Pawn->IsA(SDK::AShooterCharacter::StaticClass()) ||
            PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass()) || PC->Pawn != Human) {
            SetAutoPickupStatus("骑乘时暂停；请步行使用自动拾取");
            return;
        }

        if (g_MDX12::g_MenuState::g_isOpen || PC->bShowMouseCursor) {
            SetAutoPickupStatus("菜单、背包或游戏界面打开，已暂停");
            return;
        }
        const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
        if (!GameWindow || GetForegroundWindow() != GameWindow) {
            SetAutoPickupStatus("游戏未处于前台，已暂停");
            return;
        }

        float WeightRatio = Human->MyCharacterStatusComponent->BPGetPercentStatusValue(
            SDK::EPrimalCharacterStatusValue::Weight);
        if (!std::isfinite(WeightRatio)) {
            SetAutoPickupStatus("人物负重数据无效");
            return;
        }
        if (WeightRatio > 1.5f) WeightRatio /= 100.0f;
        WeightRatio = std::clamp(WeightRatio, 0.0f, 1.0f);
        const float MaximumWeightRatio = std::clamp(
            g_Config::AutoPickupMaxWeightPercent, 10.0f, 100.0f) / 100.0f;
        if (WeightRatio >= MaximumWeightRatio) {
            SetAutoPickupStatus(std::format(
                "负重 {:.0f}% 已达到停止阈值 {:.0f}%",
                WeightRatio * 100.0f, MaximumWeightRatio * 100.0f));
            return;
        }
        const float CurrentWeight = Human->MyCharacterStatusComponent->BPGetCurrentStatusValue(
            SDK::EPrimalCharacterStatusValue::Weight);
        const float MaximumWeight = Human->MyCharacterStatusComponent->BPGetMaxStatusValue(
            SDK::EPrimalCharacterStatusValue::Weight);
        const bool HasAbsoluteWeightData = std::isfinite(CurrentWeight) &&
            std::isfinite(MaximumWeight) && CurrentWeight >= 0.0f && MaximumWeight > 0.0f;

        SDK::UClass* DroppedItemClass = SDK::ADroppedItem::StaticClass();
        if (!DroppedItemClass) {
            SetAutoPickupStatus("掉落物类型尚未加载");
            return;
        }

        SDK::TArray<SDK::AActor*> Actors;
        SDK::UGameplayStatics::GetAllActorsOfClass(World, DroppedItemClass, &Actors);
        if (Actors.Num() < 0 || Actors.Num() > 100000) {
            SetAutoPickupStatus("掉落物列表同步异常，已跳过本轮");
            return;
        }

        const float MaximumRangeMeters = std::clamp(
            g_Config::AutoPickupRangeMeters, 1.0f, 5.0f);
        SDK::ADroppedItem* BestDroppedItem = nullptr;
        SDK::UObject* BestUseObject = nullptr;
        int32_t BestUseIndex = -1;
        uint64_t BestDroppedItemIdentity = 0;
        float BestDistanceMeters = std::numeric_limits<float>::infinity();
        std::string BestItemName;
        int32_t InRangeCount = 0;
        int32_t FilteredCount = 0;
        int32_t BlockedCount = 0;
        int32_t WeightBlockedCount = 0;

        for (int32_t ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex) {
            SDK::AActor* Actor = Actors[ActorIndex];
            if (!IsUsableObject(Actor) || !Actor->IsA(DroppedItemClass)) continue;
            auto* DroppedItem = static_cast<SDK::ADroppedItem*>(Actor);
            if (DroppedItem->bPreventPickup || !IsUsableObject(DroppedItem->MyItem) ||
                !DroppedItem->MyItem->Class || DroppedItem->MyItem->ItemQuantity <= 0) {
                continue;
            }

            const float DistanceMeters = Human->GetDistanceTo(DroppedItem) * 0.01f;
            if (!std::isfinite(DistanceMeters) || DistanceMeters > MaximumRangeMeters) continue;
            ++InRangeCount;

            int32_t DroppedItemSerialNumber = -1;
            if (!TryGetLiveObjectSerial(DroppedItem, DroppedItem->Index,
                &DroppedItemSerialNumber)) {
                ++BlockedCount;
                continue;
            }
            const uint64_t DroppedItemIdentity =
                (static_cast<uint64_t>(static_cast<uint32_t>(DroppedItem->Index)) << 32) |
                static_cast<uint32_t>(DroppedItemSerialNumber);
            const auto RetryIt = State.RetryAfter.find(DroppedItemIdentity);
            if (RetryIt != State.RetryAfter.end() && Now < RetryIt->second) continue;

            const std::string ItemClassName = DroppedItem->MyItem->Class->GetName();
            if ((DroppedItem->bIsWildEgg && !g_Config::bDroppedItemEgg) ||
                (g_Config::bAutoPickupUseDroppedItemFilter &&
                    !g_Util::IsDroppedItemAllowed(
                        ItemClassName, DroppedItem->MyItem->ItemQuantity))) {
                ++FilteredCount;
                continue;
            }

            if (HasAbsoluteWeightData) {
                const float StackWeight = DroppedItem->MyItem->GetItemWeightForInventory(
                    false, false, Human->MyInventoryComponent);
                if (std::isfinite(StackWeight) && StackWeight > 0.0f &&
                    (CurrentWeight + StackWeight) / MaximumWeight > MaximumWeightRatio) {
                    ++WeightBlockedCount;
                    continue;
                }
            }

            const float ObjectPickupRangeUnits = DroppedItem->GetMaxPickupDistance(PC);
            if (std::isfinite(ObjectPickupRangeUnits) && ObjectPickupRangeUnits > 0.0f &&
                DistanceMeters * 100.0f > ObjectPickupRangeUnits) {
                ++BlockedCount;
                continue;
            }
            if (!DroppedItem->IsAllowedToPickupItem(PC) ||
                !DroppedItem->CanCharPickUpItem(Human, false) ||
                !PC->LineOfSightTo(DroppedItem, SDK::FVector{}, false)) {
                ++BlockedCount;
                continue;
            }

            SDK::TArray<SDK::FMultiUseEntry> Entries;
            DroppedItem->GetMultiUseEntries(PC, &Entries, -1);
            if (Entries.Num() < 0 || Entries.Num() > 128) {
                ++BlockedCount;
                continue;
            }

            SDK::UObject* UseObject = nullptr;
            int32_t UseIndex = -1;
            int32_t BestEntryScore = std::numeric_limits<int32_t>::min();
            for (int32_t EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex) {
                const SDK::FMultiUseEntry& Entry = Entries[EntryIndex];
                if (Entry.bHideFromUI || Entry.bDisableUse || Entry.bClientSideOnly ||
                    Entry.UseIndex < 0) {
                    continue;
                }

                const std::string RawText = Entry.UseString.ToString();
                const std::string NormalizedText = NormalizeGachaItemName(RawText);
                const bool IsPickupEntry =
                    NormalizedText.find("pickup") != std::string::npos ||
                    RawText.find("拾取") != std::string::npos ||
                    RawText.find("捡起") != std::string::npos ||
                    RawText.find("捡取") != std::string::npos ||
                    RawText.find("拿取") != std::string::npos;
                // Current local-server disassembly confirms 100 is the
                // single-stack pickup entry. Still enumerate the live entries
                // so disabled, hidden or client-only options are never guessed.
                const bool IsKnownSinglePickupIndex = Entry.UseIndex == 100;
                if (!IsPickupEntry && !IsKnownSinglePickupIndex) continue;

                const bool IsPickupAllEntry =
                    NormalizedText.find("pickupall") != std::string::npos ||
                    NormalizedText.find("nearby") != std::string::npos ||
                    RawText.find("全部") != std::string::npos ||
                    RawText.find("附近") != std::string::npos ||
                    RawText.find("周围") != std::string::npos;
                if (IsPickupAllEntry) continue;

                const int32_t EntryScore = Entry.Priority +
                    (IsKnownSinglePickupIndex ? 10000 : 0);
                if (EntryScore <= BestEntryScore) continue;
                SDK::UObject* CandidateUseObject = IsUsableObject(Entry.ForComponent)
                    ? static_cast<SDK::UObject*>(Entry.ForComponent)
                    : static_cast<SDK::UObject*>(DroppedItem);
                if (!IsUsableObject(CandidateUseObject)) continue;
                BestEntryScore = EntryScore;
                UseObject = CandidateUseObject;
                UseIndex = Entry.UseIndex;
            }

            if (!UseObject || UseIndex < 0) {
                ++BlockedCount;
                continue;
            }
            if (DistanceMeters >= BestDistanceMeters) continue;

            BestDroppedItem = DroppedItem;
            BestUseObject = UseObject;
            BestUseIndex = UseIndex;
            BestDroppedItemIdentity = DroppedItemIdentity;
            BestDistanceMeters = DistanceMeters;
            BestItemName = DroppedItem->MyItem->DescriptiveNameBase
                ? DroppedItem->MyItem->DescriptiveNameBase.ToString()
                : ItemClassName;
        }

        if (!BestDroppedItem || !BestUseObject || BestUseIndex < 0) {
            if (InRangeCount == 0) {
                SetAutoPickupStatus(std::format(
                    "{:.1f}m 内没有可拾取掉落物（负重 {:.0f}%）",
                    MaximumRangeMeters, WeightRatio * 100.0f));
            }
            else if (WeightBlockedCount > 0 &&
                FilteredCount + BlockedCount + WeightBlockedCount >= InRangeCount) {
                SetAutoPickupStatus(std::format(
                    "范围内 {} 个；预计拾取后超过负重阈值 {} 个",
                    InRangeCount, WeightBlockedCount));
            }
            else if (FilteredCount > 0 &&
                FilteredCount + BlockedCount + WeightBlockedCount >= InRangeCount) {
                SetAutoPickupStatus(std::format(
                    "范围内 {} 个；筛选排除 {} 个，权限/视线/交互限制 {} 个",
                    InRangeCount, FilteredCount, BlockedCount));
            }
            else if (BlockedCount > 0 &&
                BlockedCount + WeightBlockedCount >= InRangeCount) {
                SetAutoPickupStatus(std::format(
                    "范围内 {} 个，但都不满足权限、视线、距离或普通拾取交互条件",
                    InRangeCount));
            }
            else {
                SetAutoPickupStatus("等待上一拾取请求确认");
            }
            return;
        }

        // The entry is generated by this exact dropped-item instance. The
        // public RPC then repeats the normal server-side distance, permission,
        // ownership and weight checks.
        // A listen server may complete the pickup synchronously and destroy the
        // actor before ProcessEvent returns, so snapshot identity before RPC.
        PC->ServerMultiUse(BestUseObject, BestUseIndex);
        State.RetryAfter[BestDroppedItemIdentity] = Now + std::chrono::milliseconds(1250);
        if (State.RetryAfter.size() > 2048) State.RetryAfter.clear();
        SetAutoPickupStatus(std::format(
            "已请求拾取 {}（{:.1f}m，负重 {:.0f}%）",
            BestItemName, BestDistanceMeters, WeightRatio * 100.0f));
    }

    std::string GetAutoPickupStatus() {
        std::scoped_lock Lock(g_AutoPickupStatusMutex);
        return g_AutoPickupStatus;
    }

    void AutoRepairItems(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct AutoRepairRuntimeState {
            SDK::UWorld* World = nullptr;
            Clock::time_point NextScan = Clock::time_point::min();
            Clock::time_point NextRequest = Clock::time_point::min();
        };
        static AutoRepairRuntimeState State;

        if (!g_Config::bAutoRepairItems) {
            State = AutoRepairRuntimeState{};
            SetAutoRepairStatus("智能自动修理未启用");
            return;
        }
        if (State.World != World) {
            State = AutoRepairRuntimeState{};
            State.World = World;
        }

        const Clock::time_point Now = Clock::now();
        if (State.NextScan != Clock::time_point::min() && Now < State.NextScan) return;
        State.NextScan = Now + std::chrono::milliseconds(250);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::APrimalCharacter* InventoryOwner = GetPlayerInventoryOwner(PC);
        if (!World || !IsUsableObject(PC) || !IsUsableObject(InventoryOwner) ||
            !InventoryOwner->IsA(SDK::AShooterCharacter::StaticClass()) ||
            InventoryOwner->IsA(SDK::APrimalDinoCharacter::StaticClass()) ||
            !IsUsableObject(InventoryOwner->MyInventoryComponent)) {
            SetAutoRepairStatus("等待人物与背包数据");
            return;
        }

        if (g_MDX12::g_MenuState::g_isOpen || PC->bShowMouseCursor) {
            SetAutoRepairStatus("菜单、背包或游戏界面打开，已暂停");
            return;
        }
        const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
        if (!GameWindow || GetForegroundWindow() != GameWindow) {
            SetAutoRepairStatus("游戏未处于前台，已暂停");
            return;
        }

        SDK::UPrimalInventoryComponent* Inventory = InventoryOwner->MyInventoryComponent;
        if (!Inventory->IsAllowedInventoryAccess(PC)) {
            SetAutoRepairStatus("当前人物背包不可访问");
            return;
        }
        if (!Inventory->IsRepairingAllowed()) {
            SetAutoRepairStatus("当前背包或服务器规则不允许修理");
            return;
        }
        if (Inventory->IsOnCraftRequestCooldown()) {
            SetAutoRepairStatus("等待服务器制造/修理冷却");
            return;
        }

        const float RepairThresholdPercent = std::clamp(
            g_Config::AutoRepairThresholdPercent, 1.0f, 95.0f);
        SDK::UPrimalItem* BestItem = nullptr;
        float BestDurabilityPercent = std::numeric_limits<float>::infinity();
        int32_t DamagedBelowThreshold = 0;
        std::unordered_set<SDK::UPrimalItem*> SeenItems;

        auto ConsiderItems = [&](const SDK::TArray<SDK::UPrimalItem*>& Items) {
            const int32_t ItemCount = Items.Num();
            if (ItemCount < 0 || ItemCount > 100000) return;
            for (int32_t ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex) {
                SDK::UPrimalItem* Item = Items[ItemIndex];
                if (!IsUsableObject(Item) || !Item->Class || !SeenItems.insert(Item).second ||
                    !Item->bUseItemDurability || Item->bIsItemSkin || Item->bIsBlueprint ||
                    Item->bIsEngram || Item->bIsRepairing) {
                    continue;
                }

                float DurabilityPercent = Item->GetDurabilityPercentage();
                if (!std::isfinite(DurabilityPercent)) continue;
                if (DurabilityPercent <= 1.5f) DurabilityPercent *= 100.0f;
                DurabilityPercent = std::clamp(DurabilityPercent, 0.0f, 100.0f);
                if (DurabilityPercent > RepairThresholdPercent || DurabilityPercent >= 99.99f) {
                    continue;
                }
                ++DamagedBelowThreshold;

                if (!Item->CanRepair(false, false, false)) continue;
                if (DurabilityPercent >= BestDurabilityPercent) continue;
                BestDurabilityPercent = DurabilityPercent;
                BestItem = Item;
            }
        };

        ConsiderItems(Inventory->EquippedItems);
        if (g_Config::bAutoRepairIncludeBackpack) {
            ConsiderItems(Inventory->InventoryItems);
        }

        if (!BestItem) {
            if (DamagedBelowThreshold > 0) {
                SetAutoRepairStatus(std::format(
                    "{} 件低于阈值，但缺少材料、工作台或修理条件",
                    DamagedBelowThreshold));
            }
            else {
                SetAutoRepairStatus(std::format(
                    "没有耐久低于 {:.0f}% 的{}",
                    RepairThresholdPercent,
                    g_Config::bAutoRepairIncludeBackpack ? "装备或背包物品" : "已装备物品"));
            }
            return;
        }

        if (State.NextRequest != Clock::time_point::min() && Now < State.NextRequest) {
            SetAutoRepairStatus(std::format(
                "等待修理请求冷却；下一件 {}（耐久 {:.0f}%）",
                BestItem->DescriptiveNameBase ? BestItem->DescriptiveNameBase.ToString() :
                    BestItem->Class->GetName(),
                BestDurabilityPercent));
            return;
        }

        const std::string ItemName = BestItem->DescriptiveNameBase
            ? BestItem->DescriptiveNameBase.ToString() : BestItem->Class->GetName();
        const SDK::FItemNetID ItemID = BestItem->ItemID;
        PC->ServerRepairItem(Inventory, ItemID);
        const float IntervalSeconds = std::clamp(
            g_Config::AutoRepairIntervalSeconds, 0.5f, 10.0f);
        State.NextRequest = Now + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(IntervalSeconds));
        SetAutoRepairStatus(std::format(
            "已请求修理 {}（耐久 {:.0f}%）；等待服务器确认",
            ItemName, BestDurabilityPercent));
    }

    std::string GetAutoRepairStatus() {
        std::scoped_lock Lock(g_AutoRepairStatusMutex);
        return g_AutoRepairStatus;
    }

    void MaintainFishingRod(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct FishingRodMaintenanceState {
            Clock::time_point NextCheck = Clock::time_point::min();
            Clock::time_point NextAction = Clock::time_point::min();
            Clock::time_point BaitMismatchSince = Clock::time_point::min();
            Clock::time_point ReequipAt = Clock::time_point::min();
            SDK::FItemNetID LastRodID{};
            SDK::FItemNetID ObservedBaitRodID{};
            SDK::FItemNetID PendingReequipRodID{};
            bool HasLastRod = false;
            bool LastRodWasBroken = false;
            bool HasObservedBaitRod = false;
            bool BaitRecoveryAttempted = false;
            bool PendingReequip = false;
            int32_t ObservedStoredBait = -1;
        };
        static FishingRodMaintenanceState State;

        if (!g_Config::bAutoSwitchFishingRod && !g_Config::bAutoRecoverFishingBait) {
            State = FishingRodMaintenanceState{};
            g_FishingRodMaintenanceStatus = "鱼竿维护未启用";
            return;
        }

        const Clock::time_point Now = Clock::now();
        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!PC || !Human || Human->IsDead() || !Human->MyInventoryComponent) {
            State.PendingReequip = false;
            g_FishingRodMaintenanceStatus = "等待人物和背包数据";
            return;
        }
        SDK::UPrimalInventoryComponent* Inventory = Human->MyInventoryComponent;

        if (State.PendingReequip) {
            if (!g_Config::bAutoRecoverFishingBait) {
                State.PendingReequip = false;
            }
            else if (Now < State.ReequipAt) {
                g_FishingRodMaintenanceStatus = "正在收起鱼竿，等待重新装备";
                return;
            }
            else {
                SDK::UPrimalItem* Rod = Inventory->BPFindItemWithID(
                    static_cast<int32_t>(State.PendingReequipRodID.ItemID1),
                    static_cast<int32_t>(State.PendingReequipRodID.ItemID2));
                State.PendingReequip = false;
                if (IsFishingRodItem(Rod) &&
                    (!Rod->bUseItemDurability || Rod->ItemDurability > 0.01f)) {
                    PC->ServerEquipPawnItem(Rod->ItemID);
                    State.NextAction = Now + std::chrono::seconds(3);
                    g_FishingRodMaintenanceStatus = "鱼饵同步异常：已重新装备当前鱼竿";
                }
                else {
                    g_FishingRodMaintenanceStatus = "重新装备失败：原鱼竿已不存在或损坏";
                }
                return;
            }
        }

        if (State.NextCheck != Clock::time_point::min() && Now < State.NextCheck) return;
        State.NextCheck = Now + std::chrono::milliseconds(250);

        SDK::AShooterWeapon* Weapon = Human->CurrentWeapon;
        const bool HasFishingWeapon = IsFishingRelatedObject(Weapon);
        SDK::UPrimalItem* CurrentRod = nullptr;
        if (HasFishingWeapon && IsFishingRodItem(Weapon->AssociatedPrimalItem)) {
            CurrentRod = Weapon->AssociatedPrimalItem;
        }
        if (!CurrentRod && HasFishingWeapon) {
            CurrentRod = Inventory->BPFindItemWithID(
                static_cast<int32_t>(Human->CurrentWeaponItemID.ItemID1),
                static_cast<int32_t>(Human->CurrentWeaponItemID.ItemID2));
            if (!IsFishingRodItem(CurrentRod)) CurrentRod = nullptr;
        }

        auto EquipReplacement = [&](const SDK::FItemNetID* ExcludedID) -> bool {
            SDK::UPrimalItem* Replacement = FindBestReplacementFishingRod(Inventory, ExcludedID);
            if (!Replacement) return false;
            if (State.NextAction != Clock::time_point::min() && Now < State.NextAction) return true;
            PC->ServerEquipPawnItem(Replacement->ItemID);
            State.NextAction = Now + std::chrono::seconds(3);
            State.LastRodID = Replacement->ItemID;
            State.HasLastRod = true;
            State.LastRodWasBroken = false;
            const std::string Name = Replacement->DescriptiveNameBase ?
                Replacement->DescriptiveNameBase.ToString() : Replacement->Class->GetName();
            g_FishingRodMaintenanceStatus = "损坏鱼竿已切换为: " + Name;
            return true;
        };

        if (!CurrentRod) {
            State.BaitMismatchSince = Clock::time_point::min();
            if (g_Config::bAutoSwitchFishingRod && State.HasLastRod) {
                SDK::UPrimalItem* LastRod = Inventory->BPFindItemWithID(
                    static_cast<int32_t>(State.LastRodID.ItemID1),
                    static_cast<int32_t>(State.LastRodID.ItemID2));
                const bool LastRodIsBroken = State.LastRodWasBroken ||
                    (IsFishingRodItem(LastRod) && LastRod->bUseItemDurability &&
                        LastRod->ItemDurability <= 0.01f);
                if (LastRodIsBroken) {
                    if (!EquipReplacement(&State.LastRodID)) {
                        g_FishingRodMaintenanceStatus = "鱼竿已损坏，但背包里没有可用的备用鱼竿";
                    }
                    return;
                }
            }
            g_FishingRodMaintenanceStatus = "等待装备鱼竿";
            return;
        }

        State.LastRodID = CurrentRod->ItemID;
        State.HasLastRod = true;
        const bool RodIsBroken = CurrentRod->bUseItemDurability && CurrentRod->ItemDurability <= 0.01f;
        State.LastRodWasBroken = RodIsBroken;
        if (RodIsBroken) {
            State.BaitMismatchSince = Clock::time_point::min();
            if (g_Config::bAutoSwitchFishingRod) {
                if (!EquipReplacement(&CurrentRod->ItemID)) {
                    g_FishingRodMaintenanceStatus = "当前鱼竿已损坏，但没有可用的备用鱼竿";
                }
            }
            else {
                g_FishingRodMaintenanceStatus = "当前鱼竿已损坏";
            }
            return;
        }

        if (!g_Config::bAutoRecoverFishingBait) {
            State.BaitMismatchSince = Clock::time_point::min();
            g_FishingRodMaintenanceStatus = "当前鱼竿耐久正常";
            return;
        }

        if (!State.HasObservedBaitRod ||
            !SameItemNetID(State.ObservedBaitRodID, CurrentRod->ItemID) ||
            State.ObservedStoredBait != CurrentRod->WeaponClipAmmo) {
            State.ObservedBaitRodID = CurrentRod->ItemID;
            State.ObservedStoredBait = CurrentRod->WeaponClipAmmo;
            State.HasObservedBaitRod = true;
            State.BaitRecoveryAttempted = false;
            State.BaitMismatchSince = Clock::time_point::min();
        }

        const int32_t LiveBait = Weapon->GetCurrentAmmoInClip();
        if (LiveBait > 0) {
            State.BaitMismatchSince = Clock::time_point::min();
            State.BaitRecoveryAttempted = false;
            g_FishingRodMaintenanceStatus = std::format("鱼竿正常：已装鱼饵 {}", LiveBait);
            return;
        }

        if (CurrentRod->WeaponClipAmmo <= 0) {
            State.BaitMismatchSince = Clock::time_point::min();
            g_FishingRodMaintenanceStatus = "当前鱼竿没有装鱼饵，不执行自动重拿";
            return;
        }

        bool ReflectedLineActive = false;
        for (const char* PropertyName : { "bIsFishing", "bFishing", "bIsLineCast",
            "bLineCast", "bHasCast", "bIsCasting", "bBobberDeployed",
            "bHasFishingLine", "bIsReeling" }) {
            bool Value = false;
            if (ReadReflectedBoolValue(Weapon, PropertyName, &Value) && Value) {
                ReflectedLineActive = true;
                break;
            }
        }
        if (Weapon->BPGetCurrentState() != SDK::EWeaponState::Idle || ReflectedLineActive) {
            State.BaitMismatchSince = Clock::time_point::min();
            g_FishingRodMaintenanceStatus = "鱼竿正在抛线或收线，不执行重新装备";
            return;
        }

        if (State.BaitMismatchSince == Clock::time_point::min()) {
            State.BaitMismatchSince = Now;
            g_FishingRodMaintenanceStatus = "检测到鱼饵计数不同步，正在确认";
            return;
        }
        if (State.BaitRecoveryAttempted) {
            g_FishingRodMaintenanceStatus = "已尝试恢复本次鱼饵异常，等待游戏同步";
            return;
        }
        if (Now - State.BaitMismatchSince < std::chrono::seconds(3) ||
            (State.NextAction != Clock::time_point::min() && Now < State.NextAction)) {
            g_FishingRodMaintenanceStatus = "鱼饵计数不同步，等待安全重拿鱼竿";
            return;
        }

        // Put the rod away locally first, then use the normal server equip RPC
        // after the weapon-switch animation has had time to clear the stale state.
        Human->GiveDefaultWeapon(true);
        State.PendingReequipRodID = CurrentRod->ItemID;
        State.PendingReequip = true;
        State.ReequipAt = Now + std::chrono::milliseconds(700);
        State.BaitRecoveryAttempted = true;
        g_FishingRodMaintenanceStatus = "鱼饵同步异常：正在重新拿起鱼竿";
    }

    std::string GetFishingRodMaintenanceStatus() {
        return g_FishingRodMaintenanceStatus;
    }

    void AutoFishingQTE(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct ExternalKeyState {
            bool Active = false;
            SDK::FKey Key{};
            Clock::time_point PressedAt = Clock::time_point::min();
        };
        static Clock::time_point LastHandledTime = Clock::time_point::min();
        static Clock::time_point NextProbe = Clock::time_point::min();
        static int32_t LastWidgetIndex = -1;
        static int32_t LastKeyIndex = -1;
        static SDK::FName LastKeyName{};
        static ExternalKeyState ExternalKey;
        static bool SawQTESequence = false;
        static Clock::time_point LastPromptSeen = Clock::time_point::min();
        static Clock::time_point LastRecast = Clock::time_point::min();
        constexpr auto HandleInterval = std::chrono::milliseconds(16);
        constexpr auto KeyHoldDuration = std::chrono::milliseconds(60);
        constexpr auto RecastDelay = std::chrono::milliseconds(1800);
        constexpr auto RecastCooldown = std::chrono::milliseconds(2500);

        const Clock::time_point Now = Clock::now();
        if (ExternalKey.Active) {
            if (!g_Config::bAutoFishingQTE ||
                Now - ExternalKey.PressedAt >= KeyHoldDuration) {
                const std::string ReleasedKey = ExternalKey.Key.KeyName.ToString();
                const bool Released = SendExternalFishingKey(ExternalKey.Key, false);
                ExternalKey = ExternalKeyState{};
                g_FishingQTEStatus = Released ?
                    "已直接模拟键盘释放: " + ReleasedKey :
                    "键盘释放事件发送失败: " + ReleasedKey;
            }
            else {
                g_FishingQTEStatus = "正在直接模拟键盘按下: " +
                    ExternalKey.Key.KeyName.ToString();
                return;
            }
        }

        if (!g_Config::bAutoFishingQTE) {
            SawQTESequence = false;
            LastPromptSeen = Clock::time_point::min();
            g_FishingQTEStatus = "自动钓鱼未启用";
            return;
        }
        if (!g_Config::bAutoFishingRecast) {
            SawQTESequence = false;
            LastPromptSeen = Clock::time_point::min();
        }

        // Subtracting time_point::min() from Now overflows the signed clock
        // duration on the first call and can make this handler return forever.
        if (LastHandledTime != Clock::time_point::min() &&
            Now - LastHandledTime < HandleInterval) return;
        if (Now < NextProbe) return;
        NextProbe = Now + HandleInterval;

        if (!World) {
            g_FishingQTEStatus = "自动钓鱼未取得游戏世界";
            return;
        }
        if (!World->OwningGameInstance) {
            g_FishingQTEStatus = "自动钓鱼等待游戏实例";
            return;
        }
        if (World->OwningGameInstance->LocalPlayers.Num() == 0) {
            g_FishingQTEStatus = "自动钓鱼等待本地玩家";
            return;
        }

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController) {
            g_FishingQTEStatus = "自动钓鱼等待玩家控制器";
            return;
        }
        if (!LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) {
            g_FishingQTEStatus = "当前控制器不是 ShooterPlayerController";
            return;
        }

        SDK::FKey CurrentKey{};
        int32_t CurrentKeyIndex = -1;
        // Check the previously found widget first, then the equipped rod.
        // Most fishing sessions expose the key on the rod, so no object-table
        // discovery is needed on those ticks.
        SDK::UObject* PromptSource = FindActiveFishingPrompt(
            false, &CurrentKey, &CurrentKeyIndex);

        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        SDK::UObject* FishingWeapon = Human ?
            ReadReflectedObjectValue(Human, "CurrentWeapon") : nullptr;
        const bool HasFishingRod = IsFishingRelatedObject(FishingWeapon);
        if (!HasFishingRod) {
            SawQTESequence = false;
            LastPromptSeen = Clock::time_point::min();
        }

        // On dedicated servers the prompt state is commonly replicated onto
        // the active WeapFishingRod_C instance, while the screen letter is only
        // a child TextBlock. Read the weapon when no rendered widget exposed a
        // key directly.
        if (!PromptSource && HasFishingRod &&
            TryReadFishingQTEKey(FishingWeapon, &CurrentKey, &CurrentKeyIndex)) {
            PromptSource = FishingWeapon;
        }

        if (!PromptSource) {
            PromptSource = FindActiveFishingPrompt(
                true, &CurrentKey, &CurrentKeyIndex);
        }

        if (!PromptSource || !PromptSource->Class) {
            const bool RecastReady = g_Config::bAutoFishingRecast && HasFishingRod &&
                SawQTESequence && LastPromptSeen != Clock::time_point::min() &&
                Now - LastPromptSeen >= RecastDelay &&
                (LastRecast == Clock::time_point::min() ||
                    Now - LastRecast >= RecastCooldown);
            if (RecastReady) {
                if (SendExternalFishingRecastClick()) {
                    SawQTESequence = false;
                    LastPromptSeen = Clock::time_point::min();
                    LastRecast = Now;
                    g_FishingQTEStatus = "已自动连竿：发送鼠标左键重新抛竿";
                }
                else {
                    g_FishingQTEStatus = "自动连竿等待关闭菜单并切回游戏前台";
                }
                return;
            }
            g_FishingQTEStatus = HasFishingRod ?
                "已检测到钓鱼竿，等待服务器同步 QTE 按键" :
                "等待装备钓鱼竿和 QTE 提示";
            return;
        }

        SawQTESequence = true;
        LastPromptSeen = Now;

        const int32_t WidgetIndex = PromptSource->Index;

        // Retry an unchanged prompt after a short delay. This covers a missed
        // synthetic input, while still allowing two consecutive identical QTE
        // letters when the widget does not expose an index property.
        if (LastWidgetIndex == WidgetIndex && LastKeyIndex == CurrentKeyIndex &&
            LastKeyName == CurrentKey.KeyName &&
            Now - LastHandledTime < std::chrono::milliseconds(250)) {
            return;
        }

        if (g_MDX12::g_MenuState::g_isOpen) {
            g_FishingQTEStatus = "已识别按键，但请先关闭 F2 菜单";
            return;
        }

        if (!SendExternalFishingKey(CurrentKey, true)) {
            g_FishingQTEStatus = "已识别 " + CurrentKey.KeyName.ToString() +
                "，但直接键盘输入发送失败";
            return;
        }

        ExternalKey.Active = true;
        ExternalKey.Key = CurrentKey;
        ExternalKey.PressedAt = Now;
        LastWidgetIndex = WidgetIndex;
        LastKeyIndex = CurrentKeyIndex;
        LastKeyName = CurrentKey.KeyName;
        LastHandledTime = Now;
        g_FishingQTEStatus = "已直接模拟键盘按下: " + CurrentKey.KeyName.ToString() +
            " / 来源: " + PromptSource->Class->GetName();
    }

    std::string GetFishingQTEStatus() {
        return g_FishingQTEStatus;
    }

    void PumpShotgunNoReload(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct PumpShotgunState {
            SDK::AShooterWeapon* Weapon = nullptr;
            int32_t ObjectIndex = -1;
            bool OriginalDontConsumeAmmo = false;
            bool HasOriginalDontConsumeAmmo = false;
        };
        static PumpShotgunState State;
        static Clock::time_point NextServerReload = Clock::time_point::min();

        auto RestoreState = [&]() {
            SDK::AShooterWeapon* Weapon = ResolveCachedObject(State.Weapon, State.ObjectIndex);
            if (Weapon && State.HasOriginalDontConsumeAmmo) {
                WriteReflectedBoolValue(Weapon, "bDontActuallyConsumeItemAmmo",
                    State.OriginalDontConsumeAmmo);
            }
            State = PumpShotgunState{};
        };

        if (!g_Config::bPumpShotgunNoReload) {
            RestoreState();
            return;
        }

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!PC || !Human) {
            RestoreState();
            return;
        }

        SDK::UObject* WeaponObject = ReadReflectedObjectValue(Human, "CurrentWeapon");
        SDK::UClass* WeaponClass = SDK::AShooterWeapon::StaticClass();
        if (!WeaponObject || !WeaponClass || !WeaponObject->IsA(WeaponClass)) {
            RestoreState();
            return;
        }

        SDK::AShooterWeapon* Weapon = static_cast<SDK::AShooterWeapon*>(WeaponObject);
        if (!IsPumpShotgun(Weapon)) {
            RestoreState();
            return;
        }

        int32_t ClipCapacity = 6;
        SDK::UPrimalItem* AssociatedItem = nullptr;
        if (SDK::UObject* ItemObject = ReadReflectedObjectValue(Weapon, "AssociatedPrimalItem")) {
            SDK::UClass* ItemClass = SDK::UPrimalItem::StaticClass();
            if (ItemClass && ItemObject->IsA(ItemClass)) {
                AssociatedItem = static_cast<SDK::UPrimalItem*>(ItemObject);
                const int32_t ReportedMaxAmmo = AssociatedItem->BPGetMaxAmmo();
                if (ReportedMaxAmmo >= 2 && ReportedMaxAmmo <= 64) ClipCapacity = ReportedMaxAmmo;
            }
        }

        if (!Human->HasAuthority()) {
            // A remote server owns the ammunition state. Use only its normal
            // reload RPC. Refill the single spent shell after each shot instead
            // of waiting for the magazine to become empty.
            RestoreState();
            const Clock::time_point Now = Clock::now();
            if (Weapon->GetCurrentAmmoInClip() < ClipCapacity && Weapon->CanReload() && Now >= NextServerReload) {
                Weapon->ServerStartReload();
                NextServerReload = Now + std::chrono::milliseconds(750);
            }
            return;
        }

        if (State.Weapon != Weapon || State.ObjectIndex != Weapon->Index) {
            RestoreState();
            State.Weapon = Weapon;
            State.ObjectIndex = Weapon->Index;
            State.HasOriginalDontConsumeAmmo = ReadReflectedBoolValue(Weapon,
                "bDontActuallyConsumeItemAmmo", &State.OriginalDontConsumeAmmo);
        }

        WriteReflectedBoolValue(Weapon, "bDontActuallyConsumeItemAmmo", true);

        if (AssociatedItem) AssociatedItem->BPSetWeaponClipAmmo(ClipCapacity);
        Weapon->SetAmmoInClip(ClipCapacity);
        WriteReflectedInt32Value(Weapon, "CurrentAmmoInClip", ClipCapacity);
    }

    std::vector<AutoCraftStationView> GetAutoCraftCatalog(SDK::UWorld* World) {
        (void)World;
        std::scoped_lock Lock(g_AutoCraftUiBridge.SnapshotMutex);
        return g_AutoCraftUiBridge.Catalog;
    }

    static void ApplyAutoCraftStationSelection(int InventoryObjectIndex) {
        if (g_AutoCraftRuntime.SelectedInventoryObjectIndex != InventoryObjectIndex) {
            g_AutoCraftRuntime.SelectedInventoryObjectIndex = InventoryObjectIndex;
            g_AutoCraftRuntime.SelectedItemObjectIndex = -1;
            g_AutoCraftRuntime.SelectedInventory = nullptr;
            g_AutoCraftRuntime.SelectedRecipeID = {};
            g_AutoCraftRuntime.SelectedRecipeClass.clear();
            g_AutoCraftRuntime.SubmittedRequests = 0;
        }
        if (InventoryObjectIndex < 0) {
            g_AutoCraftRuntime.Status = "请选择附近工作台";
            return;
        }

        SDK::UObject* InventoryObject = ResolveObjectAtIndex(InventoryObjectIndex);
        SDK::UClass* InventoryClass = SDK::UPrimalInventoryComponent::StaticClass();
        SDK::AShooterPlayerController* PC = GetLocalShooterController(SDK::UWorld::GetWorld());
        if (!PC || !InventoryObject || !InventoryClass || !InventoryObject->IsA(InventoryClass)) {
            g_AutoCraftRuntime.Status = "所选工作站已失效，请重新搜索";
            return;
        }

        SDK::UPrimalInventoryComponent* Inventory =
            static_cast<SDK::UPrimalInventoryComponent*>(InventoryObject);
        g_AutoCraftRuntime.SelectedInventory = Inventory;
        TryGetLiveObjectSerial(Inventory, Inventory->Index,
            &g_AutoCraftRuntime.SelectedInventorySerial);
        g_AutoCraftRuntime.NextInventorySync = std::chrono::steady_clock::time_point::min();
        SDK::UPrimalInventoryComponent* PreviousDiscovery = ResolveCachedObject(
            g_AutoCraftRuntime.DiscoveryInventory,
            g_AutoCraftRuntime.DiscoveryInventoryObjectIndex);
        if (PreviousDiscovery && PreviousDiscovery != Inventory) {
            PC->ServerActorCloseRemoteInventory(PreviousDiscovery);
        }

        // Default engram items are commonly not replicated until the remote
        // inventory is requested. Read the selected station in the background;
        // the server still validates tribe access and distance.
        PC->ServerActorViewRemoteInventory(Inventory);
        PC->ServerRequestActorItems(Inventory, true, true);
        g_AutoCraftRuntime.DiscoveryInventory = Inventory;
        g_AutoCraftRuntime.DiscoveryInventoryObjectIndex = Inventory->Index;
        g_AutoCraftRuntime.DiscoveryRequestedAt = std::chrono::steady_clock::now();
        g_AutoCraftRuntime.NextDiscoveryRequest =
            g_AutoCraftRuntime.DiscoveryRequestedAt + std::chrono::seconds(2);
        g_AutoCraftRuntime.NextCatalogRefresh = std::chrono::steady_clock::time_point::min();
        g_AutoCraftRuntime.Status = "已选择工作站，正在读取服务器印痕/图纸";
    }

    static void ApplyAutoCraftRecipeSelection(int ItemObjectIndex) {
        g_AutoCraftRuntime.SelectedItemObjectIndex = ItemObjectIndex;
        g_AutoCraftRuntime.SelectedRecipeID = {};
        g_AutoCraftRuntime.SelectedRecipeClass.clear();
        g_AutoCraftRuntime.SubmittedRequests = 0;
        g_AutoCraftRuntime.Scheduler = {};
        auto* Object = ResolveObjectAtIndex(ItemObjectIndex);
        if (Object && Object->IsA(SDK::UPrimalItem::StaticClass())) {
            auto* Item = static_cast<SDK::UPrimalItem*>(Object);
            int32_t ID1 = 0, ID2 = 0;
            Item->BPGetItemID(&ID1, &ID2);
            g_AutoCraftRuntime.SelectedRecipeID = {
                static_cast<uint32_t>(ID1), static_cast<uint32_t>(ID2) };
            g_AutoCraftRuntime.SelectedRecipeClass = Item->Class->GetFullName();
            g_AutoCraftRuntime.SelectedRecipeIsEngram = Item->bIsEngram;
        }
        g_AutoCraftRuntime.Status = ItemObjectIndex >= 0 ?
            "已就绪；开启自动制作后将按服务器规则循环请求" : "请选择印痕/图纸";
    }

    void SelectAutoCraftStation(int InventoryObjectIndex) {
        std::scoped_lock Lock(g_AutoCraftUiBridge.RequestMutex);
        g_AutoCraftUiBridge.StationSelectionPending = true;
        g_AutoCraftUiBridge.PendingStationObjectIndex = InventoryObjectIndex;
    }

    void SelectAutoCraftRecipe(int ItemObjectIndex) {
        std::scoped_lock Lock(g_AutoCraftUiBridge.RequestMutex);
        g_AutoCraftUiBridge.RecipeSelectionPending = true;
        g_AutoCraftUiBridge.PendingRecipeObjectIndex = ItemObjectIndex;
    }

    static void ApplyPendingAutoCraftSelections() {
        bool HasStationSelection = false;
        int StationObjectIndex = -1;
        bool HasRecipeSelection = false;
        int RecipeObjectIndex = -1;
        {
            std::scoped_lock Lock(g_AutoCraftUiBridge.RequestMutex);
            HasStationSelection = g_AutoCraftUiBridge.StationSelectionPending;
            StationObjectIndex = g_AutoCraftUiBridge.PendingStationObjectIndex;
            HasRecipeSelection = g_AutoCraftUiBridge.RecipeSelectionPending;
            RecipeObjectIndex = g_AutoCraftUiBridge.PendingRecipeObjectIndex;
            g_AutoCraftUiBridge.StationSelectionPending = false;
            g_AutoCraftUiBridge.RecipeSelectionPending = false;
        }

        if (HasStationSelection) ApplyAutoCraftStationSelection(StationObjectIndex);
        if (HasRecipeSelection) ApplyAutoCraftRecipeSelection(RecipeObjectIndex);
    }

    int GetSelectedAutoCraftStation() {
        std::scoped_lock Lock(g_AutoCraftUiBridge.SnapshotMutex);
        return g_AutoCraftUiBridge.SelectedStationObjectIndex;
    }

    int GetSelectedAutoCraftRecipe() {
        std::scoped_lock Lock(g_AutoCraftUiBridge.SnapshotMutex);
        return g_AutoCraftUiBridge.SelectedRecipeObjectIndex;
    }

    std::string GetAutoCraftStatus() {
        std::scoped_lock Lock(g_AutoCraftUiBridge.SnapshotMutex);
        return g_AutoCraftUiBridge.Status;
    }

    // ContextMenu_PrimalItem::HandleActionCraftAmount reaches this native
    // UUI_Inventory helper. Its quantity argument is capped at 100 by the game;
    // the helper emits the game's normal per-item RPCs and returns their count.
    using NativeCraftBatch = int32_t(__fastcall*)(SDK::UUI_Inventory*,
        SDK::UPrimalInventoryComponent*, bool, SDK::UPrimalItem*, int32_t);

    static NativeCraftBatch ResolveNativeCraftBatch() {
        static NativeCraftBatch Function = []() -> NativeCraftBatch {
            const auto Base = SDK::InSDKUtils::GetImageBase();
            auto* Class = SDK::UContextMenu_PrimalItem::StaticClass();
            auto* Entry = Class ? Class->GetFunction("ContextMenu_PrimalItem", "HandleActionCraftAmount") : nullptr;
            if (!Entry || reinterpret_cast<uintptr_t>(Entry->ExecFunction) != Base + 0x5c46050) return nullptr;
            const auto* Code = reinterpret_cast<const uint8_t*>(Base + 0x6f411d0);
            MEMORY_BASIC_INFORMATION Region{};
            if (!VirtualQuery(Code, &Region, sizeof(Region)) || Region.State != MEM_COMMIT ||
                (Region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                reinterpret_cast<uintptr_t>(Region.BaseAddress) + Region.RegionSize < Base + 0x6f41379)
                return nullptr;
            constexpr uint8_t Prologue[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,
                0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x41,0x56,0x41,0x57};
            constexpr uint8_t QuantityLimit[] = {0xbd,0x64,0x00,0x00,0x00,0x48,0x8b,0xcb,0x3b,0xc5,0x0f,0x4c,0xe8};
            if (std::memcmp(Code, Prologue, sizeof(Prologue)) ||
                std::memcmp(Code + 0x41, QuantityLimit, sizeof(QuantityLimit))) return nullptr;
            return reinterpret_cast<NativeCraftBatch>(Base + 0x6f411d0);
        }();
        return Function;
    }

    static SDK::UUI_Inventory* GetCraftBatchUI(SDK::AShooterPlayerController* PC) {
        auto& State = g_AutoCraftRuntime;
        auto* UI = PC->GetViewingInventoryUI();
        if (!UI) {
            auto* Hud = PC->GetShooterHud();
            if (IsUsableObject(Hud)) UI = Hud->CurrentOpenedInventory;
        }
        int32_t Serial = -1;
        if (IsUsableObject(UI) && TryGetLiveObjectSerial(UI, UI->Index, &Serial)) {
            State.BatchInventoryUI = UI;
            State.BatchUIIndex = UI->Index;
            State.BatchUISerial = Serial;
        }
        UI = State.BatchInventoryUI;
        if (!TryGetLiveObjectSerial(UI, State.BatchUIIndex, &Serial) || Serial != State.BatchUISerial ||
            !IsUsableObject(UI) || !UI->IsA(SDK::UUI_Inventory::StaticClass())) return nullptr;
        // Validate the exact owner accessor used inside the verified helper,
        // including widgets retained after the visible inventory is closed.
        using GetBatchOwner = SDK::AShooterPlayerController*(__fastcall*)(SDK::UUI_Inventory*);
        auto GetOwner = reinterpret_cast<GetBatchOwner>((*reinterpret_cast<void***>(UI))[0x5d8 / 8]);
        return GetOwner && GetOwner(UI) == PC ? UI : nullptr;
    }

    void AutoCraft(SDK::UWorld* World) {
        if (IsHusbandryInventoryActive()) return;
        if (IsUploadTerminalActive() || IsAutomaticDepositActive()) return;
        using Clock = std::chrono::steady_clock;
        AutoCraftUiSnapshotScope PublishSnapshotOnReturn;
        // Reset before applying queued UI choices, so a world change cannot
        // erase a newly selected station in the same tick.
        if (g_AutoCraftRuntime.World != World) {
            g_AutoCraftRuntime = {};
            g_AutoCraftRuntime.World = World;
        }
        ApplyPendingAutoCraftSelections();
        RefreshAutoCraftCatalog(World);
        auto& State = g_AutoCraftRuntime;
        if (ResolveNativeCraftBatch()) {
            auto* ViewingPC = GetLocalShooterController(World);
            if (ViewingPC && ViewingPC->IsViewingInventoryUI()) GetCraftBatchUI(ViewingPC);
        }
        if (!g_Config::bAutoCraft) {
            if (State.OwnsCraftRemoteView) {
                auto* PC = GetLocalShooterController(World);
                int32_t Serial = -1;
                if (PC && TryGetLiveObjectSerial(State.SelectedInventory,
                    State.SelectedInventoryObjectIndex, &Serial) &&
                    Serial == State.SelectedInventorySerial &&
                    PC->GetViewingInventory() != State.SelectedInventory) {
                    PC->ServerActorCloseRemoteInventory(State.SelectedInventory);
                }
                State.OwnsCraftRemoteView = false;
            }
            State.NextInventorySync = Clock::time_point::min();
            State.Scheduler = {};
            return;
        }

        auto* PC = GetLocalShooterController(World);
        auto* Human = GetLocalHumanCharacter(World);
        int32_t Serial = -1;
        if (!PC || !Human || Human->IsDead()) {
            State.Status = "等待人物恢复";
            return;
        }
        if (!TryGetLiveObjectSerial(State.SelectedInventory,
            State.SelectedInventoryObjectIndex, &Serial) ||
            Serial != State.SelectedInventorySerial) {
            State.Status = "等待所选工作台；工作台被销毁或重新加载后请重选";
            return;
        }
        auto* Inventory = State.SelectedInventory;
        auto* Owner = Inventory->GetOwner();
        const float Distance = Owner ? Human->GetDistanceTo(Owner) * 0.01f : INFINITY;
        if (!std::isfinite(Distance) ||
            Distance > std::clamp(g_Config::AutoCraftRangeMeters, 2.0f, 100.0f)) {
            State.Status = "工作台超出范围，返回范围后自动继续";
            return;
        }
        auto* Viewing = PC->GetViewingInventory();
        if (Viewing && Viewing != Inventory && Viewing != Human->MyInventoryComponent) {
            State.Status = "正在查看其他背包，关闭后自动继续制作";
            return;
        }
        const auto Now = Clock::now();
        if (Now >= State.NextInventorySync) {
            // Closing the ordinary UI unsubscribes queue updates. Renew the
            // selected remote view as well as requesting fresh item instances.
            PC->ServerActorViewRemoteInventory(Inventory);
            PC->ServerRequestActorItems(Inventory, true, true);
            State.OwnsCraftRemoteView = Viewing != Inventory;
            State.NextInventorySync = Now + std::chrono::seconds(3);
        }
        SDK::UPrimalItem* Item = nullptr;
        SDK::UPrimalItem* EngramFallback = nullptr;
        const auto& Items = Inventory->InventoryItems;
        if (Items.Num() >= 0 && Items.Num() <= 100000) {
            for (int32_t Index = 0; Index < Items.Num(); ++Index) {
                auto* Candidate = Items[Index];
                if (!IsUsableObject(Candidate)) continue;
                int32_t ID1 = 0, ID2 = 0;
                Candidate->BPGetItemID(&ID1, &ID2);
                if ((ID1 != 0 || ID2 != 0) &&
                    static_cast<uint32_t>(ID1) == State.SelectedRecipeID.ItemID1 &&
                    static_cast<uint32_t>(ID2) == State.SelectedRecipeID.ItemID2) {
                    Item = Candidate;
                    break;
                }
                // Only built-in engrams may recover by class. A physical
                // blueprint must retain its net ID to preserve its quality.
                if (State.SelectedRecipeIsEngram && Candidate->bIsEngram &&
                    Candidate->Class->GetFullName() == State.SelectedRecipeClass) {
                    EngramFallback = Candidate;
                }
            }
        }
        if (!Item) Item = EngramFallback;
        if (!Item) {
            State.Status = State.SelectedRecipeClass.empty() ?
                "请选择印痕/图纸" : "等待服务器重新同步所选配方，收到后自动继续";
            return;
        }
        State.SelectedItemObjectIndex = Item->Index;
        int32_t ID1 = 0, ID2 = 0;
        Item->BPGetItemID(&ID1, &ID2);
        if (ID1 == 0 && ID2 == 0) {
            State.Status = "等待配方物品 ID";
            return;
        }
        State.SelectedRecipeID = { static_cast<uint32_t>(ID1), static_cast<uint32_t>(ID2) };
        const int32_t Queue = std::max(0, Item->GetCraftQueueCount());
        const int32_t Batch = State.Scheduler.Plan(Queue, Now, g_Config::AutoCraftIntervalSeconds);
        if (Batch == 0) {
            if (Queue >= 100)
                State.Status = std::format("队列 {}；持续同步，空位后继续补单 | 累计请求 {}",
                    Queue, State.SubmittedRequests);
            return;
        }
        int32_t Submitted = 0;
        bool UsedNativeBatch = false;
        if (auto NativeBatch = ResolveNativeCraftBatch()) {
            if (auto* UI = GetCraftBatchUI(PC)) {
                Submitted = NativeBatch(UI, Inventory, Inventory != Human->MyInventoryComponent, Item, Batch);
                UsedNativeBatch = true;
            }
        }
        if (!UsedNativeBatch) {
            // A UI can be garbage-collected after closing. Keep continuous
            // crafting alive using the same server RPC until it is recreated.
            for (int32_t Index = 0; Index < Batch; ++Index)
                PC->ServerCraftItem(Inventory, State.SelectedRecipeID);
            Submitted = Batch;
        }
        Submitted = std::clamp(Submitted, 0, Batch);
        State.SubmittedRequests += Submitted;
        State.Status = std::format("{} | 队列 {} | 本批请求 {}/{} | 累计请求 {}；持续制作",
            UsedNativeBatch ? "原生批量制作" : "批量补单（打开一次工作台可使用原生入口）",
            Queue, Submitted, Batch, State.SubmittedRequests);
    }

    void DinoAttackSpeed(SDK::UWorld* World) {
        struct AttackIntervalState {
            float AttackInterval;
            float RiderAttackInterval;
        };

        struct DinoAttackSpeedState {
            SDK::APrimalDinoCharacter* Dino = nullptr;
            int32_t DinoObjectIndex = -1;
            std::vector<AttackIntervalState> OriginalIntervals;
            SDK::UAnimMontage* ActiveMontage = nullptr;
            float OriginalMontagePlayRate = 1.0f;
        };

        static DinoAttackSpeedState State;

        auto ResolveAttackLayout = [](SDK::APrimalDinoCharacter* Dino,
            ReflectedStructArrayView* View, SDK::FProperty** AttackIntervalProperty,
            SDK::FProperty** RiderAttackIntervalProperty) {
            if (!Dino || !View || !AttackIntervalProperty || !RiderAttackIntervalProperty) return false;
            if (!GetReflectedStructArray(Dino, "AttackInfos", 256, View) || !View->ElementStruct) return false;

            *AttackIntervalProperty = FindReflectedProperty(View->ElementStruct, "AttackInterval");
            *RiderAttackIntervalProperty = FindReflectedProperty(View->ElementStruct, "RiderAttackInterval");

            auto IsUsableFloatField = [View](SDK::FProperty* Property) {
                return Property && Property->ClassPrivate && Property->Offset >= 0 &&
                    Property->Offset + static_cast<int32_t>(sizeof(float)) <= View->ElementSize &&
                    Property->ElementSize >= sizeof(float) &&
                    EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "FloatProperty");
                };
            return IsUsableFloatField(*AttackIntervalProperty) &&
                IsUsableFloatField(*RiderAttackIntervalProperty);
        };

        auto RestoreState = [&ResolveAttackLayout]() {
            if (State.Dino && State.DinoObjectIndex >= 0 &&
                IsUsableObject(State.Dino) && State.Dino->Index == State.DinoObjectIndex) {
                ReflectedStructArrayView View;
                SDK::FProperty* AttackIntervalProperty = nullptr;
                SDK::FProperty* RiderAttackIntervalProperty = nullptr;
                if (ResolveAttackLayout(State.Dino, &View, &AttackIntervalProperty,
                    &RiderAttackIntervalProperty)) {
                    const int32_t RestoreCount = std::min(View.Num,
                        static_cast<int32_t>(State.OriginalIntervals.size()));
                    for (int32_t Index = 0; Index < RestoreCount; ++Index) {
                        uint8_t* Element = View.Data + static_cast<size_t>(Index) * View.ElementSize;
                        *reinterpret_cast<float*>(Element + AttackIntervalProperty->Offset) =
                            State.OriginalIntervals[Index].AttackInterval;
                        *reinterpret_cast<float*>(Element + RiderAttackIntervalProperty->Offset) =
                            State.OriginalIntervals[Index].RiderAttackInterval;
                    }
                }

                if (State.ActiveMontage && State.Dino->Mesh) {
                    if (SDK::UAnimInstance* AnimInstance = State.Dino->Mesh->GetAnimInstance()) {
                        AnimInstance->Montage_SetPlayRate(State.ActiveMontage, State.OriginalMontagePlayRate);
                    }
                }
            }

            State = DinoAttackSpeedState{};
        };

        if (!g_Config::bDinoAttackSpeed) {
            RestoreState();
            return;
        }

        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
            RestoreState();
            return;
        }

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController || !LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);
        if (!PC->Pawn || !PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::APrimalDinoCharacter* Dino = static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn);
        ReflectedStructArrayView AttackInfos;
        SDK::FProperty* AttackIntervalProperty = nullptr;
        SDK::FProperty* RiderAttackIntervalProperty = nullptr;
        if (!ResolveAttackLayout(Dino, &AttackInfos, &AttackIntervalProperty,
            &RiderAttackIntervalProperty)) {
            RestoreState();
            return;
        }

        if (State.Dino != Dino || State.DinoObjectIndex != Dino->Index) {
            RestoreState();
            State.Dino = Dino;
            State.DinoObjectIndex = Dino->Index;
            State.OriginalIntervals.reserve(static_cast<size_t>(AttackInfos.Num));

            for (int32_t Index = 0; Index < AttackInfos.Num; ++Index) {
                const uint8_t* Element = AttackInfos.Data + static_cast<size_t>(Index) * AttackInfos.ElementSize;
                State.OriginalIntervals.push_back({
                    *reinterpret_cast<const float*>(Element + AttackIntervalProperty->Offset),
                    *reinterpret_cast<const float*>(Element + RiderAttackIntervalProperty->Offset)
                    });
            }
        }

        const float ConfiguredMultiplier = g_Config::DinoAttackSpeedMultiplier;
        const float Multiplier = std::isfinite(ConfiguredMultiplier)
            ? std::clamp(ConfiguredMultiplier, 0.5f, 3.0f)
            : 1.0f;
        const int32_t ApplyCount = std::min(AttackInfos.Num,
            static_cast<int32_t>(State.OriginalIntervals.size()));

        for (int32_t Index = 0; Index < ApplyCount; ++Index) {
            uint8_t* Element = AttackInfos.Data + static_cast<size_t>(Index) * AttackInfos.ElementSize;
            const AttackIntervalState& Original = State.OriginalIntervals[Index];

            if (Original.AttackInterval > 0.0f) {
                *reinterpret_cast<float*>(Element + AttackIntervalProperty->Offset) =
                    Original.AttackInterval / Multiplier;
            }
            if (Original.RiderAttackInterval > 0.0f) {
                *reinterpret_cast<float*>(Element + RiderAttackIntervalProperty->Offset) =
                    Original.RiderAttackInterval / Multiplier;
            }
        }

        SDK::UAnimInstance* AnimInstance = Dino->Mesh ? Dino->Mesh->GetAnimInstance() : nullptr;
        SDK::UAnimMontage* CurrentMontage = Dino->IsCurrentlyPlayingAttackAnimation()
            ? Dino->GetCurrentMontage()
            : nullptr;

        if (!AnimInstance || !CurrentMontage) {
            State.ActiveMontage = nullptr;
            State.OriginalMontagePlayRate = 1.0f;
            return;
        }

        if (State.ActiveMontage != CurrentMontage) {
            if (State.ActiveMontage) {
                AnimInstance->Montage_SetPlayRate(State.ActiveMontage, State.OriginalMontagePlayRate);
            }

            State.ActiveMontage = CurrentMontage;
            State.OriginalMontagePlayRate = AnimInstance->Montage_GetPlayRate(CurrentMontage);
            if (State.OriginalMontagePlayRate <= 0.0f) {
                State.OriginalMontagePlayRate = 1.0f;
            }
        }

        AnimInstance->Montage_SetPlayRate(CurrentMontage, State.OriginalMontagePlayRate * Multiplier);
    }

    void DragontopiaSkillTuning(SDK::UWorld* World) {
        // The generated SDK is slightly older than the live game. The DataTable
        // portion is intentionally disabled, and all live movement/attack
        // fields below are resolved through Unreal reflection rather than
        // through generated, hard-coded member offsets.
        UpdateDlcSkillData(World);

        struct AttackDamageState {
            int32_t MeleeDamageAmount = 0;
            bool IsBasicAttack = false;
        };

        struct FloatPatch {
            float* Address = nullptr;
            float OriginalValue = 0.0f;
        };

        struct DragontopiaTuningState {
            SDK::APrimalDinoCharacter* Dino = nullptr;
            int32_t DinoObjectIndex = -1;
            SDK::UDracoFlightMovementComponent* Movement = nullptr;
            int32_t MovementObjectIndex = -1;
            std::vector<FloatPatch> FlightSpeed;
            std::vector<FloatPatch> DiveBoost;
            std::vector<FloatPatch> ManeuverPower;
            std::vector<FloatPatch> StaminaCost;
            std::vector<FloatPatch> Cooldown;
            std::vector<AttackDamageState> AttackDamage;
        };

        static DragontopiaTuningState State;

        auto RestoreState = []() {
            SDK::UDracoFlightMovementComponent* ValidMovement =
                ResolveCachedObject(State.Movement, State.MovementObjectIndex);
            if (ValidMovement) {
                auto RestorePatches = [](const std::vector<FloatPatch>& Patches) {
                    for (const FloatPatch& Patch : Patches) {
                        if (Patch.Address && std::isfinite(Patch.OriginalValue)) {
                            *Patch.Address = Patch.OriginalValue;
                        }
                    }
                };
                RestorePatches(State.FlightSpeed);
                RestorePatches(State.DiveBoost);
                RestorePatches(State.ManeuverPower);
                RestorePatches(State.StaminaCost);
                RestorePatches(State.Cooldown);
            }

            SDK::APrimalDinoCharacter* ValidDino = ResolveCachedObject(State.Dino, State.DinoObjectIndex);
            if (ValidDino && !State.AttackDamage.empty()) {
                ReflectedStructArrayView Attacks;
                if (GetReflectedStructArray(ValidDino, "AttackInfos", 512, &Attacks) && Attacks.ElementStruct) {
                    SDK::FProperty* DamageProperty = FindReflectedProperty(Attacks.ElementStruct, "MeleeDamageAmount");
                    if (DamageProperty && DamageProperty->ClassPrivate && DamageProperty->Offset >= 0 &&
                        DamageProperty->Offset + static_cast<int32_t>(sizeof(int32_t)) <= Attacks.ElementSize &&
                        EqualsNameIgnoreCase(DamageProperty->ClassPrivate->Name.ToString(), "IntProperty")) {
                        const int32_t Count = std::min(Attacks.Num,
                            static_cast<int32_t>(State.AttackDamage.size()));
                        for (int32_t i = 0; i < Count; ++i) {
                            uint8_t* Element = Attacks.Data + static_cast<size_t>(i) * Attacks.ElementSize;
                            *reinterpret_cast<int32_t*>(Element + DamageProperty->Offset) =
                                State.AttackDamage[i].MeleeDamageAmount;
                        }
                    }
                }
            }

            State = DragontopiaTuningState{};
        };

        if (!g_Config::bDragontopiaSkillTuning) {
            RestoreState();
            return;
        }

        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
            RestoreState();
            return;
        }

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController || !LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);
        if (!PC->Pawn || !PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::APrimalDinoCharacter* Dino = static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn);
        SDK::UPawnMovementComponent* PawnMovement = Dino->GetMovementComponent();
        if (!Dino->HasAuthority() || !PawnMovement ||
            !PawnMovement->IsA(SDK::UDracoFlightMovementComponent::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::UDracoFlightMovementComponent* Movement =
            static_cast<SDK::UDracoFlightMovementComponent*>(PawnMovement);

        if (State.Dino != Dino || State.DinoObjectIndex != Dino->Index ||
            State.Movement != Movement || State.MovementObjectIndex != Movement->Index) {
            RestoreState();

            State.Dino = Dino;
            State.DinoObjectIndex = Dino->Index;
            State.Movement = Movement;
            State.MovementObjectIndex = Movement->Index;

            auto AddPatch = [](std::vector<FloatPatch>& Group, float* Address) {
                if (!Address || !std::isfinite(*Address)) return false;
                Group.push_back({ Address, *Address });
                return true;
            };
            bool LayoutValid = true;
            LayoutValid &= AddPatch(State.FlightSpeed,
                GetReflectedNestedFloatAddress(Movement, "HoverMode", "MaxFlySpeed"));
            LayoutValid &= AddPatch(State.FlightSpeed,
                GetReflectedNestedFloatAddress(Movement, "SprintMode", "MaxFlySpeed"));
            LayoutValid &= AddPatch(State.DiveBoost,
                GetReflectedNestedFloatAddress(Movement, "HoverMode", "DiveSpeedRiseRateMultiplier"));
            LayoutValid &= AddPatch(State.DiveBoost,
                GetReflectedNestedFloatAddress(Movement, "SprintMode", "DiveSpeedRiseRateMultiplier"));
            LayoutValid &= AddPatch(State.DiveBoost, GetReflectedFloatAddress(Movement, "DiveMaxBonusSpeed"));
            LayoutValid &= AddPatch(State.DiveBoost, GetReflectedFloatAddress(Movement, "DiveSpeedRiseRate"));
            LayoutValid &= AddPatch(State.ManeuverPower, GetReflectedFloatAddress(Movement, "BarrelRollDashSpeed"));
            LayoutValid &= AddPatch(State.ManeuverPower, GetReflectedFloatAddress(Movement, "BarrelRollDashDistance"));
            LayoutValid &= AddPatch(State.StaminaCost, GetReflectedFloatAddress(Movement, "ManeuverStaminaCost"));
            LayoutValid &= AddPatch(State.StaminaCost,
                GetReflectedFloatAddress(Movement, "SprintFlightStaminaDrainPerSecond"));

            const char* ManeuverNames[] = {
                "BarrelRollLeftManeuver", "BarrelRollRightManeuver",
                "LoopBackflipManeuver", "QuickTurnManeuver"
            };
            for (const char* ManeuverName : ManeuverNames) {
                LayoutValid &= AddPatch(State.Cooldown,
                    GetReflectedNestedFloatAddress(Movement, ManeuverName, "CooldownSeconds"));
                LayoutValid &= AddPatch(State.ManeuverPower,
                    GetReflectedNestedFloatAddress(Movement, ManeuverName, "ChargeBonusPercent"));
            }

            ReflectedStructArrayView Attacks;
            if (GetReflectedStructArray(Dino, "AttackInfos", 512, &Attacks) && Attacks.ElementStruct) {
                SDK::FProperty* DamageProperty = FindReflectedProperty(Attacks.ElementStruct, "MeleeDamageAmount");
                SDK::FProperty* BasicProperty = FindReflectedProperty(Attacks.ElementStruct, "bBasicAttack");
                if (DamageProperty && DamageProperty->ClassPrivate && DamageProperty->Offset >= 0 &&
                    DamageProperty->Offset + static_cast<int32_t>(sizeof(int32_t)) <= Attacks.ElementSize &&
                    EqualsNameIgnoreCase(DamageProperty->ClassPrivate->Name.ToString(), "IntProperty") &&
                    BasicProperty) {
                    State.AttackDamage.reserve(static_cast<size_t>(Attacks.Num));
                    for (int32_t i = 0; i < Attacks.Num; ++i) {
                        uint8_t* Element = Attacks.Data + static_cast<size_t>(i) * Attacks.ElementSize;
                        bool IsBasicAttack = false;
                        if (!ReadReflectedStructBool(Element, Attacks.ElementSize, BasicProperty, &IsBasicAttack)) {
                            LayoutValid = false;
                            break;
                        }
                        State.AttackDamage.push_back({
                            *reinterpret_cast<int32_t*>(Element + DamageProperty->Offset), IsBasicAttack });
                    }
                }
            }

            if (!LayoutValid) {
                RestoreState();
                return;
            }
        }

        auto ClampFinite = [](float Value, float Minimum, float Maximum) {
            return std::isfinite(Value) ? std::clamp(Value, Minimum, Maximum) : 1.0f;
        };

        const float FlightSpeedMultiplier = ClampFinite(g_Config::DragontopiaFlightSpeedMultiplier, 0.5f, 3.0f);
        const float DiveBoostMultiplier = ClampFinite(g_Config::DragontopiaDiveBoostMultiplier, 0.5f, 3.0f);
        const float ManeuverPowerMultiplier = ClampFinite(g_Config::DragontopiaManeuverPowerMultiplier, 0.5f, 3.0f);
        const float StaminaCostMultiplier = ClampFinite(g_Config::DragontopiaStaminaCostMultiplier, 0.1f, 2.0f);
        const float CooldownMultiplier = ClampFinite(g_Config::DragontopiaCooldownMultiplier, 0.1f, 2.0f);
        const float SkillDamageMultiplier = ClampFinite(g_Config::DragontopiaSkillDamageMultiplier, 0.5f, 5.0f);

        auto ApplyPatches = [](const std::vector<FloatPatch>& Patches, float Multiplier) {
            for (const FloatPatch& Patch : Patches) {
                if (Patch.Address && std::isfinite(Patch.OriginalValue)) {
                    *Patch.Address = Patch.OriginalValue * Multiplier;
                }
            }
        };
        ApplyPatches(State.FlightSpeed, FlightSpeedMultiplier);
        ApplyPatches(State.DiveBoost, DiveBoostMultiplier);
        ApplyPatches(State.ManeuverPower, ManeuverPowerMultiplier);
        ApplyPatches(State.StaminaCost, StaminaCostMultiplier);
        ApplyPatches(State.Cooldown, CooldownMultiplier);

        if (!State.AttackDamage.empty()) {
            ReflectedStructArrayView Attacks;
            if (GetReflectedStructArray(Dino, "AttackInfos", 512, &Attacks) && Attacks.ElementStruct) {
                SDK::FProperty* DamageProperty = FindReflectedProperty(Attacks.ElementStruct, "MeleeDamageAmount");
                if (DamageProperty && DamageProperty->ClassPrivate && DamageProperty->Offset >= 0 &&
                    DamageProperty->Offset + static_cast<int32_t>(sizeof(int32_t)) <= Attacks.ElementSize &&
                    EqualsNameIgnoreCase(DamageProperty->ClassPrivate->Name.ToString(), "IntProperty")) {
                    const int32_t Count = std::min(Attacks.Num,
                        static_cast<int32_t>(State.AttackDamage.size()));
                    for (int32_t i = 0; i < Count; ++i) {
                        const AttackDamageState& Original = State.AttackDamage[i];
                        if (!Original.IsBasicAttack && Original.MeleeDamageAmount > 0) {
                            uint8_t* Element = Attacks.Data + static_cast<size_t>(i) * Attacks.ElementSize;
                            *reinterpret_cast<int32_t*>(Element + DamageProperty->Offset) = static_cast<int32_t>(
                                std::lround(static_cast<double>(Original.MeleeDamageAmount) * SkillDamageMultiplier));
                        }
                    }
                }
            }
        }
    }

    DlcSkillDataView GetDlcSkillDataView() {
        return g_DlcSkillRuntime.View;
    }

    void RequestSkillModifierDiagnostics() {
        {
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View.Status = "已排队，等待游戏线程扫描";
            g_SkillModifierDiagnostics.View.Pending = true;
        }
        g_SkillModifierDiagnostics.Requested.store(true, std::memory_order_release);
    }

    void ProcessSkillModifierDiagnostics(SDK::UWorld* World) {
        if (!g_SkillModifierDiagnostics.Requested.exchange(false, std::memory_order_acq_rel)) return;

        SkillModifierDiagnosticsView NewView;
        NewView.Status = "正在扫描";
        NewView.Pending = true;
        NewView.HasLocalAuthority = HasLocalSkillDataAuthority(World);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!PC || !IsUsableObject(PC) || !Human || !IsUsableObject(Human)) {
            NewView.Status = "未找到本地人物或玩家控制器";
            NewView.Pending = false;
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
            return;
        }

        SDK::UObject* PlayerState = ReadReflectedObjectValue(PC, "PlayerState");
        if (!PlayerState) {
            NewView.Status = "未找到玩家状态，进入服务器并完成人物加载后重试";
            NewView.Pending = false;
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
            return;
        }

        ReflectedStructValueView PlayerData;
        ReflectedStructValueView PersistentStats;
        RawArrayHeader SkillNamesHeader;
        RawArrayHeader SkillRanksHeader;
        int32_t SkillNameElementSize = 0;
        int32_t SkillRankElementSize = 0;
        if (!GetReflectedStructValue(PlayerState, "MyPlayerDataStruct", &PlayerData) ||
            !GetNestedReflectedStructValue(PlayerData, "MyPersistentCharacterStats", &PersistentStats) ||
            !GetReflectedArrayHeader(PersistentStats, "SkillUnlocks", "NameProperty", 1024,
                &SkillNamesHeader, &SkillNameElementSize) ||
            !GetReflectedArrayHeader(PersistentStats, "SkillRanks", "ByteProperty", 1024,
                &SkillRanksHeader, &SkillRankElementSize)) {
            NewView.Status = "当前版本未能反射到 SkillUnlocks / SkillRanks";
            NewView.Pending = false;
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
            return;
        }

        if (SkillNameElementSize < static_cast<int32_t>(sizeof(SDK::FName)) ||
            SkillRankElementSize < static_cast<int32_t>(sizeof(uint8_t))) {
            NewView.Status = "技能名称或等级数组元素大小不兼容";
            NewView.Pending = false;
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
            return;
        }

        const int32_t SkillCount = std::min(SkillNamesHeader.Num, SkillRanksHeader.Num);
        NewView.UnlockedSkillCount = SkillCount;
        if (SkillCount <= 0) {
            NewView.Status = "人物当前没有可扫描的已解锁技能";
            NewView.Pending = false;
            NewView.LayoutCompatible = true;
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
            return;
        }

        SDK::UFunction* SkillDataFunction = FindReflectedFunction(
            SDK::UVictoryCore::StaticClass(), "BPGetSkillData");
        SDK::UFunction* SkillModifierFunction = FindReflectedFunction(
            PC->Class, "GetSkillModifier");
        SDK::UFunction* SkillRankFunction = FindReflectedFunction(
            PC->Class, "GetSkillUnlockedAndRank");
        const bool SkillDataLayoutMatches = SkillDataFunction &&
            SkillDataFunction->Size == sizeof(SDK::Params::VictoryCore_BPGetSkillData);
        const bool SkillModifierLayoutMatches = SkillModifierFunction &&
            SkillModifierFunction->Size == sizeof(SDK::Params::ShooterPlayerController_GetSkillModifier);
        const bool SkillRankLayoutMatches = SkillRankFunction &&
            SkillRankFunction->Size == sizeof(SDK::Params::ShooterPlayerController_GetSkillUnlockedAndRank);
        NewView.LayoutCompatible = SkillDataLayoutMatches;
        if (!SkillDataLayoutMatches) {
            std::ostringstream Status;
            Status << "BPGetSkillData 参数布局不兼容（实时="
                << (SkillDataFunction ? SkillDataFunction->Size : 0)
                << "，SDK=" << sizeof(SDK::Params::VictoryCore_BPGetSkillData) << "），已停止";
            NewView.Status = Status.str();
            NewView.Pending = false;
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
            return;
        }

        SDK::UObject* ProviderCandidates[] = { Human, PlayerState, PC };
        std::unordered_set<std::string> SeenSkillNames;
        const int32_t CappedSkillCount = std::min<int32_t>(SkillCount, 256);
        for (int32_t Index = 0; Index < CappedSkillCount; ++Index) {
            const uint8_t* NameAddress = SkillNamesHeader.Data +
                static_cast<size_t>(Index) * SkillNameElementSize;
            const uint8_t* RankAddress = SkillRanksHeader.Data +
                static_cast<size_t>(Index) * SkillRankElementSize;
            const SDK::FName SkillNameValue = *reinterpret_cast<const SDK::FName*>(NameAddress);
            const std::string InternalName = SkillNameValue.ToString();
            if (InternalName.empty() || InternalName == "None" ||
                !SeenSkillNames.insert(InternalName).second) {
                continue;
            }

            int32_t UnlockedRank = static_cast<int32_t>(*RankAddress);
            if (SkillRankLayoutMatches) {
                int32_t ReportedRank = 0;
                if (PC->GetSkillUnlockedAndRank(SkillNameValue, &ReportedRank) &&
                    ReportedRank >= 0 && ReportedRank <= 128) {
                    UnlockedRank = ReportedRank;
                }
            }

            SDK::FPrimalSkill SkillData{};
            SDK::UObject* UsedProvider = nullptr;
            for (SDK::UObject* Candidate : ProviderCandidates) {
                if (!IsUsableObject(Candidate)) continue;
                SDK::FPrimalSkill CandidateData =
                    SDK::UVictoryCore::BPGetSkillData(SkillNameValue, Candidate);
                const bool HasReasonableData = CandidateData.Ranks > 0 && CandidateData.Ranks <= 128 &&
                    CandidateData.GrantedModifiersPerRank.Num() >= 0 &&
                    CandidateData.GrantedModifiersPerRank.Num() <= 128 &&
                    CandidateData.GrantedBuffPerRank.Num() >= 0 &&
                    CandidateData.GrantedBuffPerRank.Num() <= 128 &&
                    CandidateData.CooldownsPerRank.Num() >= 0 &&
                    CandidateData.CooldownsPerRank.Num() <= 128;
                if (!HasReasonableData) continue;
                SkillData = CandidateData;
                UsedProvider = Candidate;
                break;
            }
            if (!UsedProvider) continue;

            SkillDataEntryView Entry;
            Entry.InternalName = InternalName;
            Entry.DisplayName = SkillData.SkillName.ToString();
            Entry.Description = SkillData.SkillDescription.ToString();
            Entry.UnlockedRank = UnlockedRank;
            Entry.TotalRanks = SkillData.Ranks;
            Entry.GrantedBuffRanks = SkillData.GrantedBuffPerRank.Num();
            Entry.CooldownRanks = SkillData.CooldownsPerRank.Num();
            Entry.IsActiveSkill = SkillData.bIsActiveSkill;
            if (Entry.DisplayName.empty()) Entry.DisplayName = Entry.InternalName;
            if (NewView.ProviderName.empty()) NewView.ProviderName = UsedProvider->Class->GetName();

            const int32_t RankArrayCount = SkillData.GrantedModifiersPerRank.Num();
            for (int32_t RankIndex = 0; RankIndex < RankArrayCount; ++RankIndex) {
                const SDK::FItemTraitModifierArray& RankModifiers =
                    SkillData.GrantedModifiersPerRank[RankIndex];
                const int32_t ModifierCount = RankModifiers.Modifiers.Num();
                if (ModifierCount < 0 || ModifierCount > 128) continue;

                for (int32_t ModifierIndex = 0; ModifierIndex < ModifierCount; ++ModifierIndex) {
                    const SDK::FItemTraitModifier& Modifier = RankModifiers.Modifiers[ModifierIndex];
                    if (!std::isfinite(Modifier.Value)) continue;

                    SkillModifierView ModifierView;
                    ModifierView.ModifierName = Modifier.Name.ToString();
                    ModifierView.Rank = RankIndex + 1;
                    ModifierView.ConfiguredValue = Modifier.Value;
                    ModifierView.StackingType = static_cast<int>(Modifier.StackingType);
                    if (SkillModifierLayoutMatches && !ModifierView.ModifierName.empty() &&
                        ModifierView.ModifierName != "None") {
                        float EffectiveValue = 0.0f;
                        if (PC->GetSkillModifier(SkillNameValue, Modifier.Name, &EffectiveValue) &&
                            std::isfinite(EffectiveValue)) {
                            ModifierView.EffectiveValue = EffectiveValue;
                            ModifierView.HasEffectiveValue = true;
                        }
                    }
                    Entry.Modifiers.push_back(std::move(ModifierView));
                }
            }

            Entry.IsBaitFragranceCandidate = IsBaitFragranceSkillCandidate(
                Entry.InternalName, Entry.DisplayName, Entry.Description, Entry.Modifiers);
            if (Entry.IsBaitFragranceCandidate) ++NewView.BaitFragranceCandidates;
            if (!Entry.Modifiers.empty()) ++NewView.SkillsWithModifiers;
            ++NewView.SkillsWithData;
            NewView.Skills.push_back(std::move(Entry));
        }

        std::sort(NewView.Skills.begin(), NewView.Skills.end(),
            [](const SkillDataEntryView& Left, const SkillDataEntryView& Right) {
                if (Left.IsBaitFragranceCandidate != Right.IsBaitFragranceCandidate) {
                    return Left.IsBaitFragranceCandidate;
                }
                return Left.DisplayName < Right.DisplayName;
            });

        NewView.Pending = false;
        if (NewView.SkillsWithData == 0) {
            NewView.Status = "读取到技能名，但技能提供者未返回有效 FPrimalSkill 数据";
        }
        else if (NewView.BaitFragranceCandidates > 0) {
            NewView.Status = "扫描完成，已找到“饵香四溢”候选技能";
        }
        else {
            NewView.Status = "扫描完成，未自动匹配“饵香四溢”；可检查下方内部名和倍率";
        }

        const int CapturedSkills = NewView.SkillsWithData;
        const int CandidateCount = NewView.BaitFragranceCandidates;
        {
            std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
            g_SkillModifierDiagnostics.View = std::move(NewView);
        }
        g_LogManager::AddLog(120, 230, 190, 255,
            std::format("技能倍率扫描完成：{} 个技能，{} 个饵香四溢候选",
                CapturedSkills, CandidateCount));
    }

    SkillModifierDiagnosticsView GetSkillModifierDiagnostics() {
        std::lock_guard<std::mutex> Lock(g_SkillModifierDiagnostics.Mutex);
        return g_SkillModifierDiagnostics.View;
    }

    std::string BuildSkillModifierDiagnosticsReport() {
        const SkillModifierDiagnosticsView View = GetSkillModifierDiagnostics();
        std::ostringstream Report;
        Report << "Lost Colony skill modifier diagnostics\n"
            << "Status: " << View.Status << "\n"
            << "Provider: " << (View.ProviderName.empty() ? "unknown" : View.ProviderName) << "\n"
            << "Local authority: " << (View.HasLocalAuthority ? "yes" : "no") << "\n"
            << "Unlocked: " << View.UnlockedSkillCount
            << ", data: " << View.SkillsWithData
            << ", modifier skills: " << View.SkillsWithModifiers
            << ", bait candidates: " << View.BaitFragranceCandidates << "\n\n";

        Report << std::fixed << std::setprecision(6);
        for (const SkillDataEntryView& Skill : View.Skills) {
            Report << (Skill.IsBaitFragranceCandidate ? "[BAIT-CANDIDATE] " : "")
                << Skill.DisplayName << " | " << Skill.InternalName
                << " | unlocked " << Skill.UnlockedRank << "/" << Skill.TotalRanks
                << " | active=" << (Skill.IsActiveSkill ? "yes" : "no")
                << " | buffRanks=" << Skill.GrantedBuffRanks
                << " | cooldownRanks=" << Skill.CooldownRanks << "\n";
            if (!Skill.Description.empty()) Report << "  Description: " << Skill.Description << "\n";
            if (Skill.Modifiers.empty()) {
                Report << "  Modifiers: none (effect may be implemented by a buff or Blueprint)\n";
            }
            for (const SkillModifierView& Modifier : Skill.Modifiers) {
                Report << "  Rank " << Modifier.Rank << " | " << Modifier.ModifierName
                    << " = " << Modifier.ConfiguredValue
                    << " | stacking=" << GetTraitModifierStackingName(Modifier.StackingType);
                if (Modifier.HasEffectiveValue) Report << " | effective=" << Modifier.EffectiveValue;
                Report << "\n";
            }
            Report << "\n";
        }
        return Report.str();
    }

    void TastyBaitSkillOverride(SDK::UWorld* World) {
        const float Rank1Value = std::clamp(
            std::isfinite(g_Config::TastyBaitRank1ReductionPercent) ?
                g_Config::TastyBaitRank1ReductionPercent * 0.01f : 0.3f,
            0.0f, 0.95f);
        const float Rank2Value = std::clamp(
            std::isfinite(g_Config::TastyBaitRank2ReductionPercent) ?
                g_Config::TastyBaitRank2ReductionPercent * 0.01f : 0.6f,
            0.0f, 0.95f);
        g_TastyBaitOverrideRuntime.Rank1Value.store(Rank1Value, std::memory_order_relaxed);
        g_TastyBaitOverrideRuntime.Rank2Value.store(Rank2Value, std::memory_order_relaxed);

        if (!g_Config::bTastyBaitOverride) {
            if (g_TastyBaitOverrideRuntime.Controller ||
                g_TastyBaitOverrideRuntime.Provider ||
                g_TastyBaitOverrideRuntime.Enabled.load(std::memory_order_acquire)) {
                RemoveTastyBaitProcessEventHook();
                SetTastyBaitOverrideStatus("饵香四溢倍率覆盖已关闭并恢复原查询");
            }
            return;
        }

        const auto Now = std::chrono::steady_clock::now();
        if (Now < g_TastyBaitOverrideRuntime.NextRefresh) return;
        g_TastyBaitOverrideRuntime.NextRefresh = Now + std::chrono::milliseconds(500);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        if (!PC || !IsUsableObject(PC)) {
            RemoveTastyBaitProcessEventHook();
            SetTastyBaitOverrideStatus("未找到本地玩家控制器");
            return;
        }
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!Human || !IsUsableObject(Human)) {
            RemoveTastyBaitProcessEventHook();
            SetTastyBaitOverrideStatus("未找到本地人物技能 Provider");
            return;
        }

        SDK::AShooterPlayerController* HookedController = ResolveCachedObject(
            g_TastyBaitOverrideRuntime.Controller,
            g_TastyBaitOverrideRuntime.ControllerObjectIndex);
        if (g_TastyBaitOverrideRuntime.Controller && HookedController != PC) {
            RemoveTastyBaitProcessEventHook();
        }
        SDK::AShooterCharacter* HookedProvider = ResolveCachedObject(
            g_TastyBaitOverrideRuntime.Provider,
            g_TastyBaitOverrideRuntime.ProviderObjectIndex);
        if (g_TastyBaitOverrideRuntime.Provider && HookedProvider != Human) {
            RemoveTastyBaitProcessEventHook();
        }

        SDK::UFunction* ModifierFunction = FindReflectedFunction(PC->Class, "GetSkillModifier");
        SDK::UFunction* RankFunction = FindReflectedFunction(PC->Class, "GetSkillUnlockedAndRank");
        SDK::UClass* ProviderInterfaceClass = SDK::ISkillTreeProviderInterface::StaticClass();
        SDK::UFunction* ProviderModifierFunction = FindReflectedFunction(
            ProviderInterfaceClass, "GetProviderSkillModifier");
        SDK::UFunction* BPProviderModifierFunction = FindReflectedFunction(
            ProviderInterfaceClass, "BPGetProviderSkillModifier");
        if (!ModifierFunction ||
            ModifierFunction->Size != sizeof(SDK::Params::ShooterPlayerController_GetSkillModifier) ||
            !RankFunction ||
            RankFunction->Size != sizeof(SDK::Params::ShooterPlayerController_GetSkillUnlockedAndRank)) {
            RemoveTastyBaitProcessEventHook();
            SetTastyBaitOverrideStatus(std::format(
                "控制器接口不兼容：倍率 {}/{}，等级 {}/{}",
                ModifierFunction ? ModifierFunction->Size : 0,
                sizeof(SDK::Params::ShooterPlayerController_GetSkillModifier),
                RankFunction ? RankFunction->Size : 0,
                sizeof(SDK::Params::ShooterPlayerController_GetSkillUnlockedAndRank)));
            return;
        }

        const bool NativeProviderCompatible = ProviderModifierFunction &&
            ProviderModifierFunction->Size ==
                sizeof(SDK::Params::SkillTreeProviderInterface_GetProviderSkillModifier);
        const bool BlueprintProviderCompatible = BPProviderModifierFunction &&
            BPProviderModifierFunction->Size ==
                sizeof(SDK::Params::SkillTreeProviderInterface_BPGetProviderSkillModifier);
        if (!NativeProviderCompatible && !BlueprintProviderCompatible) {
            RemoveTastyBaitProcessEventHook();
            SetTastyBaitOverrideStatus(std::format(
                "Provider 接口不兼容：原生 {}/{}，BP {}/{}",
                ProviderModifierFunction ? ProviderModifierFunction->Size : 0,
                sizeof(SDK::Params::SkillTreeProviderInterface_GetProviderSkillModifier),
                BPProviderModifierFunction ? BPProviderModifierFunction->Size : 0,
                sizeof(SDK::Params::SkillTreeProviderInterface_BPGetProviderSkillModifier)));
            return;
        }
        if (!NativeProviderCompatible) ProviderModifierFunction = nullptr;
        if (!BlueprintProviderCompatible) BPProviderModifierFunction = nullptr;

        static SDK::FName TastyBaitName =
            SDK::BasicFilesImplUtils::StringToName(L"TastyBait");
        int32_t CurrentRank = 0;
        if (!PC->GetSkillUnlockedAndRank(TastyBaitName, &CurrentRank) ||
            CurrentRank < 1 || CurrentRank > 2) {
            RemoveTastyBaitProcessEventHook();
            SetTastyBaitOverrideStatus("当前人物未解锁 TastyBait，或等级不是 1/2");
            return;
        }
        g_TastyBaitOverrideRuntime.CurrentRank.store(CurrentRank, std::memory_order_relaxed);

        if (!g_TastyBaitOverrideRuntime.Controller) {
            g_TastyBaitOverrideRuntime.Controller = PC;
            g_TastyBaitOverrideRuntime.ControllerObjectIndex = PC->Index;
            g_TastyBaitOverrideRuntime.GetSkillModifierFunction = ModifierFunction;
            g_TastyBaitOverrideRuntime.ControllerOriginalProcessEvent = nullptr;
            if (!ezVMT::CreateHook(
                reinterpret_cast<void*>(&HookedTastyBaitProcessEvent), PC,
                static_cast<size_t>(SDK::Offsets::ProcessEventIdx),
                reinterpret_cast<void**>(&g_TastyBaitOverrideRuntime.ControllerOriginalProcessEvent)) ||
                !g_TastyBaitOverrideRuntime.ControllerOriginalProcessEvent ||
                !ezVMT::EnableHook(reinterpret_cast<void*>(&HookedTastyBaitProcessEvent))) {
                RemoveTastyBaitProcessEventHook();
                SetTastyBaitOverrideStatus("无法安装玩家控制器技能查询钩子");
                return;
            }

            g_TastyBaitOverrideRuntime.Provider = Human;
            g_TastyBaitOverrideRuntime.ProviderObjectIndex = Human->Index;
            g_TastyBaitOverrideRuntime.GetProviderSkillModifierFunction = ProviderModifierFunction;
            g_TastyBaitOverrideRuntime.BPGetProviderSkillModifierFunction = BPProviderModifierFunction;
            g_TastyBaitOverrideRuntime.ProviderOriginalProcessEvent = nullptr;
            if (!ezVMT::CreateHook(
                reinterpret_cast<void*>(&HookedTastyBaitProviderProcessEvent), Human,
                static_cast<size_t>(SDK::Offsets::ProcessEventIdx),
                reinterpret_cast<void**>(&g_TastyBaitOverrideRuntime.ProviderOriginalProcessEvent)) ||
                !g_TastyBaitOverrideRuntime.ProviderOriginalProcessEvent ||
                !ezVMT::EnableHook(reinterpret_cast<void*>(&HookedTastyBaitProviderProcessEvent))) {
                RemoveTastyBaitProcessEventHook();
                SetTastyBaitOverrideStatus("无法安装人物 Provider 技能查询钩子");
                return;
            }

            g_TastyBaitOverrideRuntime.ControllerQueryCalls.store(0, std::memory_order_relaxed);
            g_TastyBaitOverrideRuntime.ControllerQueryHits.store(0, std::memory_order_relaxed);
            g_TastyBaitOverrideRuntime.ProviderQueryCalls.store(0, std::memory_order_relaxed);
            g_TastyBaitOverrideRuntime.ProviderQueryHits.store(0, std::memory_order_relaxed);
            g_TastyBaitOverrideRuntime.Enabled.store(true, std::memory_order_release);
        }
        else {
            g_TastyBaitOverrideRuntime.GetSkillModifierFunction = ModifierFunction;
            g_TastyBaitOverrideRuntime.GetProviderSkillModifierFunction = ProviderModifierFunction;
            g_TastyBaitOverrideRuntime.BPGetProviderSkillModifierFunction = BPProviderModifierFunction;
            g_TastyBaitOverrideRuntime.Enabled.store(true, std::memory_order_release);
        }

        const float ActivePercent = (CurrentRank == 1 ? Rank1Value : Rank2Value) * 100.0f;
        const uint64_t ControllerCalls =
            g_TastyBaitOverrideRuntime.ControllerQueryCalls.load(std::memory_order_relaxed);
        const uint64_t ControllerHits =
            g_TastyBaitOverrideRuntime.ControllerQueryHits.load(std::memory_order_relaxed);
        const uint64_t ProviderCalls =
            g_TastyBaitOverrideRuntime.ProviderQueryCalls.load(std::memory_order_relaxed);
        const uint64_t ProviderHits =
            g_TastyBaitOverrideRuntime.ProviderQueryHits.load(std::memory_order_relaxed);
        SetTastyBaitOverrideStatus(std::format(
            "等级 {} -> {:.1f}% | 控制器 {}/{} | Provider {}/{}（精确命中/总查询）",
            CurrentRank, ActivePercent, ControllerHits, ControllerCalls,
            ProviderHits, ProviderCalls));
    }

    std::string GetTastyBaitOverrideStatus() {
        std::lock_guard<std::mutex> Lock(g_TastyBaitOverrideRuntime.StatusMutex);
        return g_TastyBaitOverrideRuntime.Status;
    }

    namespace {
        struct AutoHarvestSkippedTarget {
            SDK::FVector Location{};
            std::chrono::steady_clock::time_point Until{};
        };

        struct AutoHarvestProbeFrame {
            SDK::FVector CollisionCenter{}, FootLocation{};
            float CapsuleRadius = 75.0f, CapsuleHalfHeight = 100.0f;
            float MaxStepHeight = 45.0f, WalkableFloorZ = 0.55f;
        };

        struct AutoHarvestRuntimeState {
            bool WasEnabled = false;
            bool TekStriderMode = false;
            // The excavation rig is a persistent mode. Once automation turns
            // it on, keep it enabled across target changes and approach phases.
            bool TekStriderBeamStartRequested = false;
            SDK::APrimalDinoCharacter* TekStriderBeamDino = nullptr;
            int32_t TekStriderBeamDinoObjectIndex = -1;
            int32_t TekStriderBeamDinoSerialNumber = -1;
            bool OwnsForceTurn = false;
            bool HasTarget = false;
            bool AttackHeld = false;
            bool OwnsSprint = false;
            SDK::APrimalDinoCharacter* SprintDino = nullptr;
            int32_t SprintDinoObjectIndex = -1;
            int32_t SprintDinoSerialNumber = -1;
            SDK::UWorld* World = nullptr;
            SDK::APrimalDinoCharacter* ControlledDino = nullptr;
            int32_t WorldIndex = -1, WorldSerial = -1, DinoIndex = -1, DinoSerial = -1;
            SDK::FVector TargetLocation{};
            SDK::FVector AvoidDirection{};
            size_t SteeringPairCursor = 0;
            SDK::FVector MovementSampleLocation{};
            SDK::FRotator DesiredMountFacingRotation{};
            bool HasDesiredMountFacingRotation = false;
            std::string TargetType;
            std::vector<SDK::FVector> NavigationPoints;
            std::vector<double> NavigationRemaining;
            size_t NavigationCandidateCursor = 0;
            std::vector<g_DrawESP::ResourceTargetCandidate> SelectionCandidates;
            size_t SelectionCursor = 0;
            SDK::FVector SelectionOrigin{};
            float SelectionRadius = 0, SelectionRange = 0;
            bool SelectionHasBest = false;
            Harvest::RouteCost SelectionBestCost{false, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            g_DrawESP::ResourceTargetCandidate SelectionBest;
            std::vector<SDK::FVector> SelectionBestRoute;
            unsigned EmptySearches = 0, RangeRefinements = 0;
            float MeleeApproachLimitMeters = std::numeric_limits<float>::infinity();
            float BeamApproachLimitMeters = std::numeric_limits<float>::infinity();
            float NativeMeleeReachMeters = 0, CachedRangeMultiplier = 0;
            bool MeleeReachReady = false, CachedRangeEnabled = false;
            AutoHarvestProbeFrame CachedProbeFrame;
            SDK::FVector CachedProbeActorLocation{};
            std::vector<AutoHarvestSkippedTarget> SkippedTargets;
            size_t NavigationPointIndex = 0;
            bool NavigationPathAvailable = false;
            bool NavigationPathPartial = false;
            std::chrono::steady_clock::time_point NextUpdate{};
            std::chrono::steady_clock::time_point NextTargetSearch{};
            std::chrono::steady_clock::time_point NextTargetResolve{}, NextProbeGeometry{}, NextPathSmoothing{}, LastRangeAdjustment{};
            std::chrono::steady_clock::time_point NextMeleeReach{};
            std::chrono::steady_clock::time_point NextAttack{};
            std::chrono::steady_clock::time_point NextTekStriderBeamToggle{};
            std::chrono::steady_clock::time_point ReleaseAttack{};
            std::chrono::steady_clock::time_point TargetLastSeen{};
            std::chrono::steady_clock::time_point TargetAcquired{};
            std::chrono::steady_clock::time_point AttackStarted{};
            std::chrono::steady_clock::time_point NextPathRefresh{};
            std::chrono::steady_clock::time_point AvoidUntil{};
            std::chrono::steady_clock::time_point UnsafeSince{};
            std::chrono::steady_clock::time_point MovementSampleTime{};
            std::chrono::steady_clock::time_point StationarySince{};
            std::chrono::steady_clock::time_point NextRecoveryAttempt{};
            std::chrono::steady_clock::time_point LastApproachProgress{};
            std::chrono::steady_clock::time_point LayerMismatchSince{};
            std::chrono::steady_clock::time_point NextSprintRequest{};
            std::chrono::steady_clock::time_point ObstacleClearUntil{};
            std::chrono::steady_clock::time_point NextObstacleAttack{};
            std::chrono::steady_clock::time_point ObstacleClearCooldownUntil{};
            std::chrono::steady_clock::time_point NextHarvestProgressSample{};
            std::chrono::steady_clock::time_point LastHarvestProgress{};
            std::chrono::steady_clock::time_point AquaticSettleUntil{};
            std::chrono::steady_clock::time_point AquaticBrakeUntil{};
            SDK::FVector AquaticLastApproachDirection{};
            double BestApproachDistanceUU = std::numeric_limits<double>::infinity();
            bool ApproachProgressUsesNavigation = false;
            double AquaticClosestDistanceUU = std::numeric_limits<double>::infinity();
            int RecoveryAttempts = 0;
            int ObstacleClearAttacksRemaining = 0;
            int ObstacleClearBursts = 0;
            int AquaticOvershootCount = 0;
            Harvest::InventoryProgress InventoryProgress;
        };

        AutoHarvestRuntimeState s_autoHarvestState{};

        struct AutoHarvestAquaticPitchState {
            SDK::APrimalDinoCharacter* Dino = nullptr;
            int32_t DinoObjectIndex = -1;
            int32_t DinoSerialNumber = -1;
            SDK::UPrimalCharacterMovement* Movement = nullptr;
            int32_t MovementObjectIndex = -1;
            int32_t MovementSerialNumber = -1;
            bool UseControllerRotationPitch = false;
            bool UseControllerRotationYaw = false;
            bool ForceAccelerationFollowsRotationInSwimming = false;
            bool IgnoreRotationAccelerationWhenSwimming = false;
            bool ZeroPitchWhenNoAcceleration = false;
        };

        AutoHarvestAquaticPitchState s_autoHarvestAquaticPitchState{};
        std::mutex s_autoHarvestStatusMutex;
        std::string s_autoHarvestStatus = "未启用";

        struct AutoHarvestViewLockSnapshot {
            bool Active = false;
            SDK::FVector TargetLocation{};
            SDK::APrimalDinoCharacter* Dino = nullptr;
            int32_t DinoObjectIndex = -1;
            int32_t DinoSerialNumber = -1;
        };

        std::mutex s_autoHarvestViewLockMutex;
        AutoHarvestViewLockSnapshot s_autoHarvestViewLock{};

        double AutoHarvestDistanceSquared(const SDK::FVector& Left,
            const SDK::FVector& Right);
        bool IsFiniteAutoHarvestVector(const SDK::FVector& Value);

        void SetAutoHarvestStatus(std::string Status) {
            std::scoped_lock Lock(s_autoHarvestStatusMutex);
            s_autoHarvestStatus = std::move(Status);
        }

        void ResetAutoHarvestMeleeReach() {
            if (s_autoHarvestState.MeleeReachReady || s_autoHarvestState.CachedRangeEnabled) ResetDinoHarvestRange();
            s_autoHarvestState.MeleeReachReady = false;s_autoHarvestState.CachedRangeEnabled = false;
            s_autoHarvestState.NativeMeleeReachMeters = 0;s_autoHarvestState.NextMeleeReach = {};
        }

        void ClearAutoHarvestViewLock() {
            std::scoped_lock Lock(s_autoHarvestViewLockMutex);
            s_autoHarvestViewLock = {};
        }

        void PublishAutoHarvestViewLock(SDK::APrimalDinoCharacter* Dino,
            const SDK::FVector& TargetLocation) {
            if (!Dino || !IsFiniteAutoHarvestVector(TargetLocation)) {
                ClearAutoHarvestViewLock();
                return;
            }
            int32_t SerialNumber = -1;
            if (!TryGetLiveObjectSerial(Dino, Dino->Index, &SerialNumber)) {
                ClearAutoHarvestViewLock();
                return;
            }
            std::scoped_lock Lock(s_autoHarvestViewLockMutex);
            s_autoHarvestViewLock.Active = true;
            s_autoHarvestViewLock.TargetLocation = TargetLocation;
            s_autoHarvestViewLock.Dino = Dino;
            s_autoHarvestViewLock.DinoObjectIndex = Dino->Index;
            s_autoHarvestViewLock.DinoSerialNumber = SerialNumber;
        }

        bool SendAutoHarvestMouseButton(bool Pressed) {
            INPUT Input{};
            Input.type = INPUT_MOUSE;
            Input.mi.dwFlags = Pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            return SendInput(1, &Input, sizeof(INPUT)) == 1;
        }

        void ReleaseAutoHarvestAttack() {
            if (!s_autoHarvestState.AttackHeld) return;
            SendAutoHarvestMouseButton(false);
            s_autoHarvestState.AttackHeld = false;
        }

        bool SendAutoHarvestTekStriderRigToggle() {
            const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
            if (!GameWindow || GetForegroundWindow() != GameWindow ||
                g_MDX12::g_MenuState::g_isOpen) {
                return false;
            }

            INPUT Inputs[2]{};
            Inputs[0].type = INPUT_MOUSE;
            Inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            Inputs[1].type = INPUT_MOUSE;
            Inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            return SendInput(2, Inputs, sizeof(INPUT)) == 2;
        }

        void ForgetAutoHarvestTekStriderBeamRequest() {
            s_autoHarvestState.TekStriderBeamStartRequested = false;
            s_autoHarvestState.TekStriderBeamDino = nullptr;
            s_autoHarvestState.TekStriderBeamDinoObjectIndex = -1;
            s_autoHarvestState.TekStriderBeamDinoSerialNumber = -1;
        }

        bool StopAutoHarvestTekStriderBeamIfOwned() {
            if (!s_autoHarvestState.TekStriderBeamStartRequested) return true;

            const auto Now = std::chrono::steady_clock::now();
            if (Now < s_autoHarvestState.NextTekStriderBeamToggle) return false;

            SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
            int32_t LiveBeamDinoSerialNumber = -1;
            const bool StillControllingOwner = IsUsableObject(LocalPC) &&
                IsUsableObject(LocalPC->Pawn) &&
                LocalPC->Pawn == s_autoHarvestState.TekStriderBeamDino &&
                s_autoHarvestState.TekStriderBeamDinoObjectIndex >= 0 &&
                LocalPC->Pawn->Index ==
                    s_autoHarvestState.TekStriderBeamDinoObjectIndex &&
                TryGetLiveObjectSerial(LocalPC->Pawn,
                    s_autoHarvestState.TekStriderBeamDinoObjectIndex,
                    &LiveBeamDinoSerialNumber) &&
                LiveBeamDinoSerialNumber ==
                    s_autoHarvestState.TekStriderBeamDinoSerialNumber;
            if (!StillControllingOwner) {
                // Never send a blind right-click after a dismount/map change.
                // The old rig owns its own replicated state at that point.
                ForgetAutoHarvestTekStriderBeamRequest();
                return true;
            }

            if (!SendAutoHarvestTekStriderRigToggle()) return false;
            s_autoHarvestState.NextTekStriderBeamToggle =
                Now + std::chrono::milliseconds(750);
            ForgetAutoHarvestTekStriderBeamRequest();
            return true;
        }

        void ReleaseAutoHarvestSprint() {
            int32_t Serial = -1;
            if (s_autoHarvestState.OwnsSprint &&
                s_autoHarvestState.SprintDino &&
                s_autoHarvestState.SprintDinoObjectIndex >= 0 &&
                TryGetLiveObjectSerial(s_autoHarvestState.SprintDino,
                    s_autoHarvestState.SprintDinoObjectIndex, &Serial) && Serial == s_autoHarvestState.SprintDinoSerialNumber &&
                IsUsableObject(s_autoHarvestState.SprintDino) &&
                s_autoHarvestState.SprintDino->Index ==
                    s_autoHarvestState.SprintDinoObjectIndex) {
                s_autoHarvestState.SprintDino->OnStopRunning();
            }
            s_autoHarvestState.OwnsSprint = false;
            s_autoHarvestState.SprintDino = nullptr;
            s_autoHarvestState.SprintDinoObjectIndex = -1;
            s_autoHarvestState.SprintDinoSerialNumber = -1;
            s_autoHarvestState.NextSprintRequest = {};
        }

        void UpdateAutoHarvestSprint(SDK::APrimalDinoCharacter* Dino,
            bool ShouldSprint, std::chrono::steady_clock::time_point Now) {
            if (!ShouldSprint || !Dino || !IsUsableObject(Dino)) {
                ReleaseAutoHarvestSprint();
                return;
            }

            int32_t Serial = -1;
            if (!TryGetLiveObjectSerial(Dino, Dino->Index, &Serial)) { ReleaseAutoHarvestSprint(); return; }
            if (s_autoHarvestState.SprintDino != Dino ||
                s_autoHarvestState.SprintDinoObjectIndex != Dino->Index || s_autoHarvestState.SprintDinoSerialNumber != Serial) {
                ReleaseAutoHarvestSprint();
                s_autoHarvestState.SprintDino = Dino;
                s_autoHarvestState.SprintDinoObjectIndex = Dino->Index;
                s_autoHarvestState.SprintDinoSerialNumber = Serial;
            }

            if (Dino->IsRunning()) return;

            // If the player was already sprinting, do not take ownership. If
            // running stops while auto approach is active, start it and own only
            // that state so cleanup never cancels the player's original sprint.
            s_autoHarvestState.OwnsSprint = true;
            if (Now >= s_autoHarvestState.NextSprintRequest) {
                Dino->OnStartRunning();
                s_autoHarvestState.NextSprintRequest =
                    Now + std::chrono::seconds(1);
            }
        }

        double AutoHarvestDistanceSquared(const SDK::FVector& Left, const SDK::FVector& Right);
        bool IsFiniteAutoHarvestVector(const SDK::FVector& Value);

        void AddAutoHarvestSkippedTarget(const SDK::FVector& Location,
            std::chrono::steady_clock::time_point Until) {
            if (!IsFiniteAutoHarvestVector(Location)) return;
            constexpr double MergeDistanceSquared = 300.0 * 300.0;
            for (AutoHarvestSkippedTarget& Target : s_autoHarvestState.SkippedTargets) {
                if (AutoHarvestDistanceSquared(Target.Location, Location) <= MergeDistanceSquared) {
                    Target.Location = Location;
                    Target.Until = std::max(Target.Until, Until);
                    return;
                }
            }
            // Dense cave shelves may contain many adjacent nodes on the same
            // unreachable layer. Keep enough exclusions to avoid immediately
            // cycling back through the same cluster.
            if (s_autoHarvestState.SkippedTargets.size() >= 48) {
                auto Oldest = std::min_element(s_autoHarvestState.SkippedTargets.begin(),
                    s_autoHarvestState.SkippedTargets.end(),
                    [](const AutoHarvestSkippedTarget& Left,
                        const AutoHarvestSkippedTarget& Right) {
                            return Left.Until < Right.Until;
                    });
                if (Oldest != s_autoHarvestState.SkippedTargets.end()) {
                    s_autoHarvestState.SkippedTargets.erase(Oldest);
                }
            }
            s_autoHarvestState.SkippedTargets.push_back({ Location, Until });
        }

        std::vector<SDK::FVector> GetActiveAutoHarvestSkippedLocations(
            std::chrono::steady_clock::time_point Now) {
            std::erase_if(s_autoHarvestState.SkippedTargets,
                [Now](const AutoHarvestSkippedTarget& Target) {
                    return Target.Until <= Now || !IsFiniteAutoHarvestVector(Target.Location);
                });
            std::vector<SDK::FVector> Locations;
            Locations.reserve(s_autoHarvestState.SkippedTargets.size());
            for (const AutoHarvestSkippedTarget& Target : s_autoHarvestState.SkippedTargets) {
                Locations.push_back(Target.Location);
            }
            return Locations;
        }

        double AutoHarvestDistanceSquared(const SDK::FVector& Left, const SDK::FVector& Right) {
            const double X = Left.X - Right.X;
            const double Y = Left.Y - Right.Y;
            const double Z = Left.Z - Right.Z;
            return X * X + Y * Y + Z * Z;
        }

        bool IsFiniteAutoHarvestVector(const SDK::FVector& Value) {
            return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
        }

        bool TryGetAutoHarvestInventorySnapshot(SDK::APrimalDinoCharacter* Dino,
            Harvest::InventoryProgress::Snapshot* Out) {
            if (!Out || !IsUsableObject(Dino) || !IsUsableObject(Dino->MyInventoryComponent)) return false;
            const auto& Items = Dino->MyInventoryComponent->InventoryItems;
            if (Items.Num() < 0 || Items.Num() > 10000) return false;
            Out->clear();
            // Group stacks by item class. Stack order/merges and food depletion
            // must not hide gains in a different harvested resource type.
            for (auto* Item : Items) {
                if (!IsUsableObject(Item) || !IsUsableObject(Item->Class)) return false;
                if (Item->ItemQuantity > 0)
                    (*Out)[reinterpret_cast<uintptr_t>(Item->Class)] += Item->ItemQuantity;
            }
            return true;
        }

        void RestoreAutoHarvestAquaticPitchControl() {
            AutoHarvestAquaticPitchState& State = s_autoHarvestAquaticPitchState;
            int32_t Serial = -1;
            if (State.Dino && State.DinoObjectIndex >= 0 &&
                TryGetLiveObjectSerial(State.Dino, State.DinoObjectIndex, &Serial) && Serial == State.DinoSerialNumber &&
                IsUsableObject(State.Dino) && State.Dino->Index == State.DinoObjectIndex) {
                if (State.Dino->bUseControllerRotationPitch) State.Dino->bUseControllerRotationPitch = State.UseControllerRotationPitch;
                if (State.Dino->bUseControllerRotationYaw) State.Dino->bUseControllerRotationYaw = State.UseControllerRotationYaw;
            }
            if (State.Movement && State.MovementObjectIndex >= 0 &&
                TryGetLiveObjectSerial(State.Movement, State.MovementObjectIndex, &Serial) && Serial == State.MovementSerialNumber &&
                IsUsableObject(State.Movement) &&
                State.Movement->Index == State.MovementObjectIndex) {
                if (State.Movement->bForceAccelerationFollowsRotationInSwimming) State.Movement->bForceAccelerationFollowsRotationInSwimming =
                    State.ForceAccelerationFollowsRotationInSwimming;
                if (State.Movement->bIgnoreRotationAccelerationWhenSwimming) State.Movement->bIgnoreRotationAccelerationWhenSwimming =
                    State.IgnoreRotationAccelerationWhenSwimming;
                if (!State.Movement->bZeroPitchWhenNoAcceleration) State.Movement->bZeroPitchWhenNoAcceleration =
                    State.ZeroPitchWhenNoAcceleration;
            }
            State = AutoHarvestAquaticPitchState{};
        }

        void ApplyAutoHarvestAquaticPitchControl(SDK::APrimalDinoCharacter* Dino) {
            if (!Dino || !Dino->CharacterMovement ||
                !IsUsableObject(Dino->CharacterMovement)) {
                RestoreAutoHarvestAquaticPitchControl();
                return;
            }

            SDK::UPrimalCharacterMovement* Movement =
                static_cast<SDK::UPrimalCharacterMovement*>(Dino->CharacterMovement);
            int32_t DinoSerial = -1, MovementSerial = -1;
            if (!TryGetLiveObjectSerial(Dino, Dino->Index, &DinoSerial) ||
                !TryGetLiveObjectSerial(Movement, Movement->Index, &MovementSerial)) { RestoreAutoHarvestAquaticPitchControl(); return; }
            AutoHarvestAquaticPitchState& State = s_autoHarvestAquaticPitchState;
            if (State.Dino != Dino || State.DinoObjectIndex != Dino->Index ||
                State.Movement != Movement || State.MovementObjectIndex != Movement->Index ||
                State.DinoSerialNumber != DinoSerial || State.MovementSerialNumber != MovementSerial) {
                RestoreAutoHarvestAquaticPitchControl();
                State.Dino = Dino;
                State.DinoObjectIndex = Dino->Index;
                State.DinoSerialNumber = DinoSerial;
                State.Movement = Movement;
                State.MovementObjectIndex = Movement->Index;
                State.MovementSerialNumber = MovementSerial;
                State.UseControllerRotationPitch = Dino->bUseControllerRotationPitch;
                State.UseControllerRotationYaw = Dino->bUseControllerRotationYaw;
                State.ForceAccelerationFollowsRotationInSwimming =
                    Movement->bForceAccelerationFollowsRotationInSwimming;
                State.IgnoreRotationAccelerationWhenSwimming =
                    Movement->bIgnoreRotationAccelerationWhenSwimming;
                State.ZeroPitchWhenNoAcceleration = Movement->bZeroPitchWhenNoAcceleration;
            }

            // Water dinos need controller pitch explicitly enabled. Otherwise
            // control rotation contains a valid pitch but the pawn/movement
            // layer silently flattens it, so only yaw is visible in game.
            Dino->bUseControllerRotationPitch = true;
            Dino->bUseControllerRotationYaw = true;
            Movement->bForceAccelerationFollowsRotationInSwimming = true;
            Movement->bIgnoreRotationAccelerationWhenSwimming = true;
            Movement->bZeroPitchWhenNoAcceleration = false;
        }

        bool MakeHorizontalAutoHarvestDirection(const SDK::FVector& Start,
            const SDK::FVector& End, SDK::FVector* OutDirection) {
            if (!OutDirection) return false;
            const double X = End.X - Start.X;
            const double Y = End.Y - Start.Y;
            const double Length = std::sqrt(X * X + Y * Y);
            if (!std::isfinite(Length) || Length <= 1.0) return false;
            *OutDirection = SDK::FVector{ X / Length, Y / Length, 0.0 };
            return true;
        }

        bool MakeThreeDimensionalAutoHarvestDirection(const SDK::FVector& Start,
            const SDK::FVector& End, SDK::FVector* OutDirection) {
            if (!OutDirection) return false;
            const double X = End.X - Start.X;
            const double Y = End.Y - Start.Y;
            const double Z = End.Z - Start.Z;
            const double Length = std::sqrt(X * X + Y * Y + Z * Z);
            if (!std::isfinite(Length) || Length <= 1.0) return false;
            *OutDirection = SDK::FVector{ X / Length, Y / Length, Z / Length };
            return true;
        }

        SDK::FVector RotateAutoHarvestDirection(const SDK::FVector& Direction, float Degrees) {
            constexpr double Pi = 3.14159265358979323846;
            const double Radians = static_cast<double>(Degrees) * Pi / 180.0;
            const double Cosine = std::cos(Radians);
            const double Sine = std::sin(Radians);
            return SDK::FVector{
                Direction.X * Cosine - Direction.Y * Sine,
                Direction.X * Sine + Direction.Y * Cosine,
                0.0
            };
        }

        void PopulateAutoHarvestTraceIgnoreActors(
            SDK::APrimalDinoCharacter* Dino,
            SDK::TArray<SDK::AActor*>& IgnoreActors) {
            if (!Dino || IgnoreActors.Max() <= 0 || !IgnoreActors.GetDataPtr()) return;

            const auto AddUniqueActor = [&IgnoreActors](SDK::AActor* Actor) {
                if (Actor && IsUsableObject(Actor) &&
                    !IgnoreActors.Contains(Actor)) {
                    IgnoreActors.Add(Actor);
                }
            };

            // Avoid GetAttachedActors here: the generated SDK returns a shallow
            // owning TArray without a destructor, so polling it would leak the
            // engine allocation. Walk the live component attachment tree into
            // fixed local storage instead; cross-actor root components expose
            // Strider rigs, saddles and rider equipment through GetOwner().
            std::array<SDK::USceneComponent*, 192> PendingComponents{};
            size_t PendingCount = 0;
            const auto QueueComponent = [&](SDK::USceneComponent* Component) {
                if (!Component || !IsUsableObject(Component) ||
                    PendingCount >= PendingComponents.size()) return;
                for (size_t Index = 0; Index < PendingCount; ++Index) {
                    if (PendingComponents[Index] == Component) return;
                }
                PendingComponents[PendingCount++] = Component;
            };
            const auto QueueActor = [&](SDK::AActor* Actor) {
                if (!Actor || !IsUsableObject(Actor)) return;
                AddUniqueActor(Actor);
                QueueComponent(Actor->RootComponent);
                const int32_t ChildCount = Actor->Children.Num();
                if (ChildCount < 0 || ChildCount > 256) return;
                for (int32_t Index = 0; Index < ChildCount && Index < 64; ++Index) {
                    SDK::AActor* Child = Actor->Children[Index];
                    AddUniqueActor(Child);
                    if (Child && IsUsableObject(Child)) {
                        QueueComponent(Child->RootComponent);
                    }
                }
            };

            QueueActor(Dino);
            QueueActor(Dino->Rider.Get());
            for (size_t Cursor = 0; Cursor < PendingCount; ++Cursor) {
                SDK::USceneComponent* Component = PendingComponents[Cursor];
                if (!Component || !IsUsableObject(Component)) continue;
                AddUniqueActor(Component->GetOwner());

                const auto QueueChildren = [&](const auto& Children) {
                    const int32_t ChildCount = Children.Num();
                    if (ChildCount < 0 || ChildCount > 512) return;
                    for (int32_t Index = 0; Index < ChildCount && Index < 128; ++Index) {
                        QueueComponent(Children[Index]);
                    }
                };
                QueueChildren(Component->AttachChildren);
                QueueChildren(Component->ClientAttachedChildren);
            }
        }

        AutoHarvestProbeFrame ResolveAutoHarvestProbeFrame(
            SDK::APrimalDinoCharacter* Dino, const SDK::FVector& ActorLocation) {
            const auto Now = Harvest::Clock::now();
            if (Dino == s_autoHarvestState.ControlledDino && Now < s_autoHarvestState.NextProbeGeometry) {
                auto Frame = s_autoHarvestState.CachedProbeFrame;
                const auto Delta = ActorLocation - s_autoHarvestState.CachedProbeActorLocation;
                Frame.CollisionCenter = Frame.CollisionCenter + Delta;
                Frame.FootLocation = Frame.FootLocation + Delta;
                return Frame;
            }
            AutoHarvestProbeFrame Frame{};
            Frame.CollisionCenter = ActorLocation;

            bool HasValidCapsule = false;
            if (Dino && Dino->CapsuleComponent &&
                IsUsableObject(Dino->CapsuleComponent)) {
                const float Radius = Dino->CapsuleComponent->GetScaledCapsuleRadius();
                const float HalfHeight =
                    Dino->CapsuleComponent->GetScaledCapsuleHalfHeight();
                const SDK::FVector Center =
                    Dino->CapsuleComponent->K2_GetComponentLocation();
                if (std::isfinite(Radius) && Radius > 1.0f &&
                    std::isfinite(HalfHeight) && HalfHeight > 1.0f &&
                    IsFiniteAutoHarvestVector(Center)) {
                    Frame.CapsuleRadius = std::clamp(Radius, 25.0f, 2500.0f);
                    Frame.CapsuleHalfHeight =
                        std::clamp(HalfHeight, Frame.CapsuleRadius, 5000.0f);
                    Frame.CollisionCenter = Center;
                    HasValidCapsule = true;
                }
            }

            if (!HasValidCapsule && Dino) {
                SDK::FVector BoundsOrigin{};
                SDK::FVector BoundsExtent{};
                Dino->GetActorBounds(true, &BoundsOrigin, &BoundsExtent, false);
                if (IsFiniteAutoHarvestVector(BoundsOrigin) &&
                    IsFiniteAutoHarvestVector(BoundsExtent) &&
                    BoundsExtent.X > 1.0 && BoundsExtent.Y > 1.0 &&
                    BoundsExtent.Z > 1.0) {
                    Frame.CollisionCenter = BoundsOrigin;
                    Frame.CapsuleRadius = static_cast<float>(std::clamp(
                        std::max(BoundsExtent.X, BoundsExtent.Y), 25.0, 2500.0));
                    Frame.CapsuleHalfHeight = static_cast<float>(std::clamp(
                        BoundsExtent.Z, static_cast<double>(Frame.CapsuleRadius), 5000.0));
                }
            }

            if (Dino && Dino->CharacterMovement &&
                IsUsableObject(Dino->CharacterMovement)) {
                const float StepHeight = Dino->CharacterMovement->MaxStepHeight;
                if (std::isfinite(StepHeight) && StepHeight > 0.0f) {
                    Frame.MaxStepHeight = std::clamp(StepHeight, 20.0f, 400.0f);
                }
                const float FloorZ = Dino->CharacterMovement->K2_GetWalkableFloorZ();
                if (std::isfinite(FloorZ) && FloorZ > 0.05f && FloorZ < 1.0f) {
                    Frame.WalkableFloorZ = FloorZ;
                }
            }

            Frame.FootLocation = Frame.CollisionCenter;
            Frame.FootLocation.Z -= Frame.CapsuleHalfHeight;
            if (Dino == s_autoHarvestState.ControlledDino) {
                s_autoHarvestState.CachedProbeFrame = Frame;
                s_autoHarvestState.CachedProbeActorLocation = ActorLocation;
                s_autoHarvestState.NextProbeGeometry = Now + std::chrono::milliseconds(200);
            }
            return Frame;
        }

        bool IsAutoHarvestWalkableHit(SDK::APrimalDinoCharacter* Dino,
            const SDK::FHitResult& Hit, float FallbackWalkableFloorZ) {
            if (Dino && Dino->CharacterMovement &&
                IsUsableObject(Dino->CharacterMovement)) {
                return Dino->CharacterMovement->IsWalkable(Hit, true);
            }
            return Hit.ImpactNormal.Z >= FallbackWalkableFloorZ;
        }

        bool IsAutoHarvestBreakableObstacle(const SDK::FHitResult& Hit);

        bool IsAutoHarvestHardBeamOccluder(const SDK::FHitResult& Hit) {
            SDK::UPrimitiveComponent* Component = Hit.Component.Get();
            SDK::UObject* HitObject = Hit.HitObjectHandle.ReferenceObject.Get();
            SDK::AActor* Actor = Component ? Component->GetOwner() : nullptr;
            if (!Actor && HitObject && IsUsableObject(HitObject) &&
                HitObject->IsA(SDK::AActor::StaticClass())) {
                Actor = static_cast<SDK::AActor*>(HitObject);
            }

            if (Actor && IsUsableObject(Actor) &&
                Actor->IsA(SDK::APrimalStructure::StaticClass())) {
                return true;
            }

            std::string Identity;
            const auto AppendIdentity = [&Identity](SDK::UObject* Object) {
                if (!Object || !IsUsableObject(Object)) return;
                Identity += Object->GetFullName();
                Identity += ' ';
                Identity += Object->GetName();
                Identity += ' ';
                if (Object->Class) Identity += Object->Class->GetName();
                Identity += ' ';
            };
            AppendIdentity(Component);
            AppendIdentity(Actor);
            AppendIdentity(HitObject);
            std::transform(Identity.begin(), Identity.end(), Identity.begin(),
                [](unsigned char Character) {
                    return static_cast<char>(std::tolower(Character));
                });
            constexpr std::array<const char*, 10> HardOccluderNames{
                "landscape", "terrain", "cliff", "wall", "pillar",
                "foundation", "ceiling", "doorframe", "gateway", "structure"
            };
            return std::any_of(HardOccluderNames.begin(), HardOccluderNames.end(),
                [&](const char* Name) {
                    return Identity.find(Name) != std::string::npos;
                });
        }

        bool AutoHarvestHasClearBeamPath(SDK::UWorld* World,
            const SDK::FVector& BeamOrigin, const SDK::FVector& Target,
            double TargetHitToleranceUU,
            const SDK::TArray<SDK::AActor*>& IgnoreActors,
            SDK::FHitResult* OutBlockingHit = nullptr) {
            if (OutBlockingHit) *OutBlockingHit = {};
            if (!World || !IsFiniteAutoHarvestVector(BeamOrigin) ||
                !IsFiniteAutoHarvestVector(Target)) {
                return false;
            }

            const double BeamDistanceSquared = AutoHarvestDistanceSquared(
                BeamOrigin, Target);
            if (!std::isfinite(BeamDistanceSquared)) return false;
            const double ToleranceUU = std::clamp(TargetHitToleranceUU,
                75.0, 350.0);
            if (BeamDistanceSquared <= ToleranceUU * ToleranceUU) return true;

            SDK::FHitResult Hit{};
            const bool HasHit = SDK::UKismetSystemLibrary::LineTraceSingle(
                World, BeamOrigin, Target, SDK::ETraceTypeQuery::TraceTypeQuery1,
                true, IgnoreActors, SDK::EDrawDebugTrace::None, &Hit, true,
                SDK::FLinearColor(0, 0, 0, 0),
                SDK::FLinearColor(0, 0, 0, 0), 0.0f);
            if (!HasHit || !Hit.bBlockingHit) return true;

            // A tree or another harvestable node in front of the selected
            // resource is a valid beam contact: the persistent rig can remove
            // it and expose the original target. Only non-harvestable geometry
            // must force a navigation detour.
            const bool HarvestableBeamContact =
                IsAutoHarvestBreakableObstacle(Hit);
            const bool HardOccluder = !HarvestableBeamContact &&
                IsAutoHarvestHardBeamOccluder(Hit);
            const double HitToTargetSquared = IsFiniteAutoHarvestVector(Hit.ImpactPoint)
                ? AutoHarvestDistanceSquared(Hit.ImpactPoint, Target)
                : std::numeric_limits<double>::infinity();
            const bool HitSelectedNode = HarvestableBeamContact ||
                (!HardOccluder &&
                ((std::isfinite(HitToTargetSquared) &&
                    HitToTargetSquared <= ToleranceUU * ToleranceUU) ||
                    (std::isfinite(Hit.Time) && Hit.Time >= 0.965f)));
            if (HitSelectedNode) return true;

            if (OutBlockingHit) *OutBlockingHit = Hit;
            return false;
        }

        bool RefreshAutoHarvestNavigationPath(SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino, const AutoHarvestProbeFrame& ProbeFrame,
            const SDK::FVector& Start, const SDK::FVector& End,
            float HarvestRangeMeters, bool RequireTargetVisibility,
            const SDK::TArray<SDK::AActor*>& IgnoreActors) {
            // Keep the current route while trying alternatives. A transient
            // projection failure must not make a moving mount abandon its path.
            const bool HadPath = s_autoHarvestState.NavigationPathAvailable &&
                s_autoHarvestState.NavigationPointIndex < s_autoHarvestState.NavigationPoints.size();
            if (!World || !Dino) return HadPath;
            auto* Navigation = SDK::UNavigationSystemV1::GetNavigationSystem(World);
            if (!IsUsableObject(Navigation) || (!IsUsableObject(Navigation->MainNavData) &&
                (Navigation->NavDataSet.Num() <= 0 || Navigation->NavDataSet.Num() > 128))) return HadPath;
            const double Range = PhoenixInterface::FiniteClamp(HarvestRangeMeters, 1.5f,
                PhoenixInterface::MaxHarvestBeamMeters, 4.5f) * 100.;
            const double Margin = std::clamp(double(ProbeFrame.CapsuleRadius) * .35 + 75., 125., 450.);
            const double VisibleRange = std::max(100., Range - Margin);
            const double Radius = std::clamp(Range * .68, std::min(200., VisibleRange), VisibleRange);
            SDK::FVector TowardStart{};
            if (!MakeHorizontalAutoHarvestDirection(End, Start, &TowardStart)) return HadPath;
            const SDK::FVector Extent{std::max(200., double(ProbeFrame.CapsuleRadius) * 1.75),
                std::max(200., double(ProbeFrame.CapsuleRadius) * 1.75),
                std::max(300., double(ProbeFrame.CapsuleHalfHeight) * 1.5)};
            const double Tolerance = std::clamp(double(ProbeFrame.CapsuleRadius) * .35, 125., 300.);
            const auto Distance = [](const SDK::FVector& A, const SDK::FVector& B) {
                return std::sqrt(AutoHarvestDistanceSquared(A, B));
            };
            double BestScore = std::numeric_limits<double>::infinity();
            bool BestPartial = true;
            std::vector<SDK::FVector> Best;
            if (HadPath && !RequireTargetVisibility && s_autoHarvestState.UnsafeSince == Harvest::Time{} &&
                s_autoHarvestState.StationarySince == Harvest::Time{} && s_autoHarvestState.LayerMismatchSince == Harvest::Time{}) {
                BestPartial = s_autoHarvestState.NavigationPathPartial;
                BestScore = BestPartial ? 2000000. : 0.;
                auto Previous = Start;
                for (size_t I = s_autoHarvestState.NavigationPointIndex; I < s_autoHarvestState.NavigationPoints.size(); ++I) {
                    BestScore += Distance(Previous, s_autoHarvestState.NavigationPoints[I]);
                    Previous = s_autoHarvestState.NavigationPoints[I];
                }
                BestScore += Distance(Previous, End) * .25;
            }
            constexpr std::array<float, 12> Angles{0,-30,30,-60,60,-90,90,-120,120,-150,150,180};
            std::array<SDK::FVector, Harvest::NavigationQueriesPerRefresh> Queried{};
            unsigned Queries = 0;
            const auto Began = Harvest::Clock::now();
            // The ring cursor persists across failed/partial refreshes, spreading
            // the former thirteen synchronous path queries over several updates.
            for (unsigned Sample = 0; Sample < 4 && Queries < Harvest::NavigationQueriesPerRefresh; ++Sample) {
                if (Sample && Harvest::Clock::now() - Began > std::chrono::milliseconds(2)) break;
                const float Angle = Angles[s_autoHarvestState.NavigationCandidateCursor++ % Angles.size()];
                const auto Direction = RotateAutoHarvestDirection(TowardStart, Angle);
                const auto Candidate = End + Direction * Radius;
                SDK::FVector Projected{};
                if (!SDK::UNavigationSystemV1::K2_ProjectPointToNavigation(World, Candidate, &Projected,
                    Navigation->MainNavData, SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr), Extent) ||
                    !IsFiniteAutoHarvestVector(Projected) ||
                    std::abs(Projected.Z - End.Z) > std::max(350., double(ProbeFrame.CapsuleHalfHeight) * 1.5)) continue;
                bool Duplicate = false;
                for (unsigned I = 0; I < Queries; ++I)
                    Duplicate |= AutoHarvestDistanceSquared(Projected, Queried[I]) < 50. * 50.;
                if (Duplicate) continue;
                Queried[Queries++] = Projected;
                auto* Path = SDK::UNavigationSystemV1::FindPathToLocationSynchronously(World, Start,
                    Projected, Dino, SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr));
                if (!IsUsableObject(Path) || !Path->IsValid() || Path->PathPoints.Num() < 2 || Path->PathPoints.Num() > 512) continue;
                std::vector<SDK::FVector> Points;
                Points.reserve(Path->PathPoints.Num());
                double Length = 0;
                auto Previous = Start;
                bool Valid = true;
                for (const auto& Point : Path->PathPoints) {
                    if (!IsFiniteAutoHarvestVector(Point)) { Valid = false; break; }
                    Length += Distance(Previous, Point); Points.push_back(Point); Previous = Point;
                }
                if (!Valid || !std::isfinite(Length) || Points.size() < 2) continue;
                const double Remaining = Distance(Points.back(), End);
                if (RequireTargetVisibility) {
                    if (Remaining > VisibleRange) continue;
                    auto Origin = Points.back();
                    Origin.Z += std::clamp(double(ProbeFrame.CapsuleHalfHeight) * 1.15, 250., 2200.);
                    if (!AutoHarvestHasClearBeamPath(World, Origin, End, Tolerance, IgnoreActors)) continue;
                }
                const bool Partial = Path->IsPartial();
                const double Score = Length + Remaining * .25 + (Partial ? 2000000. : 0.);
                if (Score < BestScore) { Best = std::move(Points); BestScore = Score; BestPartial = Partial; }
                // A direct complete standing-position route is sufficient; avoid
                // exploring the far side of the resource for a marginal saving.
                if (!Partial && Length <= std::max(0., Distance(Start, End) - Radius) * 1.2 + ProbeFrame.CapsuleRadius) break;
            }
            if (!Best.empty()) {
                s_autoHarvestState.NavigationPoints = std::move(Best);
                s_autoHarvestState.NavigationRemaining = Harvest::PathSuffix(s_autoHarvestState.NavigationPoints, Distance);
                s_autoHarvestState.NavigationPointIndex = 1;
                s_autoHarvestState.NavigationPathAvailable = true;
                s_autoHarvestState.NavigationPathPartial = BestPartial;
                s_autoHarvestState.NextPathSmoothing = {};
                return true;
            }
            if (!HadPath) {
                s_autoHarvestState.NavigationPoints.clear(); s_autoHarvestState.NavigationRemaining.clear();
                s_autoHarvestState.NavigationPathAvailable = false; s_autoHarvestState.NavigationPathPartial = false;
            }
            return HadPath;
        }

        SDK::FVector GetAutoHarvestNavigationGoal(const SDK::FVector& CurrentLocation,
            const SDK::FVector& FinalTarget, float CapsuleRadius, SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino) {
            if (!s_autoHarvestState.NavigationPathAvailable ||
                s_autoHarvestState.NavigationPoints.empty()) {
                return FinalTarget;
            }

            const double ReachDistanceUU = std::clamp(
                static_cast<double>(CapsuleRadius) * 0.8, 175.0, 500.0);
            const double ReachDistanceSquared = ReachDistanceUU * ReachDistanceUU;
            while (s_autoHarvestState.NavigationPointIndex <
                s_autoHarvestState.NavigationPoints.size()) {
                SDK::FVector FlatCurrent = CurrentLocation;
                SDK::FVector FlatPoint = s_autoHarvestState.NavigationPoints[
                    s_autoHarvestState.NavigationPointIndex];
                FlatPoint.Z = FlatCurrent.Z;
                if (AutoHarvestDistanceSquared(FlatCurrent, FlatPoint) > ReachDistanceSquared) break;
                ++s_autoHarvestState.NavigationPointIndex;
            }

            if (s_autoHarvestState.NavigationPointIndex <
                s_autoHarvestState.NavigationPoints.size()) {
                if (g_Config::bAutoHarvestExperimentalNavigation && World && Dino &&
                    Harvest::Clock::now() >= s_autoHarvestState.NextPathSmoothing) {
                    s_autoHarvestState.NextPathSmoothing = Harvest::Clock::now() + std::chrono::milliseconds(180);
                    // Navigation raycasts cheaply determine whether intermediate
                    // corners can be skipped. Looking only a few points ahead
                    // smooths zig-zags without cutting across non-navigable space.
                    const size_t First = s_autoHarvestState.NavigationPointIndex;
                    const size_t Last = std::min(
                        s_autoHarvestState.NavigationPoints.size() - 1, First + 2);
                    for (size_t CandidateIndex = Last;
                        CandidateIndex > First; --CandidateIndex) {
                        const SDK::FVector& Candidate =
                            s_autoHarvestState.NavigationPoints[CandidateIndex];
                        const double CandidateDistanceSquared =
                            AutoHarvestDistanceSquared(CurrentLocation, Candidate);
                        const double MaximumLookAheadUU = std::clamp(
                            static_cast<double>(CapsuleRadius) * 10.0, 1800.0, 4500.0);
                        if (!std::isfinite(CandidateDistanceSquared) ||
                            CandidateDistanceSquared >
                                MaximumLookAheadUU * MaximumLookAheadUU) {
                            continue;
                        }
                        SDK::FVector NavHit{};
                        const bool NavBlocked =
                            SDK::UNavigationSystemV1::NavigationRaycast(
                                World, CurrentLocation, Candidate, &NavHit,
                                SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr),
                                nullptr);
                        if (!NavBlocked) {
                            s_autoHarvestState.NavigationPointIndex = CandidateIndex;
                            break;
                        }
                    }
                }
                return s_autoHarvestState.NavigationPoints[
                    s_autoHarvestState.NavigationPointIndex];
            }
            return FinalTarget;
        }

        double GetAutoHarvestRemainingPathDistance(const SDK::FVector& CurrentLocation,
            const SDK::FVector& FinalTarget) {
            auto Distance = [](const SDK::FVector& Start, const SDK::FVector& End) {
                const double DistanceSquared = AutoHarvestDistanceSquared(Start, End);
                return std::isfinite(DistanceSquared)
                    ? std::sqrt(std::max(0.0, DistanceSquared))
                    : std::numeric_limits<double>::infinity();
                };

            if (!s_autoHarvestState.NavigationPathAvailable ||
                s_autoHarvestState.NavigationPoints.empty() ||
                s_autoHarvestState.NavigationPointIndex >=
                s_autoHarvestState.NavigationPoints.size()) {
                return Distance(CurrentLocation, FinalTarget);
            }

            if (s_autoHarvestState.NavigationRemaining.size() != s_autoHarvestState.NavigationPoints.size())
                s_autoHarvestState.NavigationRemaining = Harvest::PathSuffix(s_autoHarvestState.NavigationPoints, Distance);
            const auto Index = s_autoHarvestState.NavigationPointIndex;
            const double RemainingDistance = Distance(CurrentLocation, s_autoHarvestState.NavigationPoints[Index]) +
                s_autoHarvestState.NavigationRemaining[Index];
            const double FinalSegmentDistance = Distance(s_autoHarvestState.NavigationPoints.back(), FinalTarget);
            return std::isfinite(FinalSegmentDistance)
                ? RemainingDistance + FinalSegmentDistance
                : std::numeric_limits<double>::infinity();
        }

        bool AutoHarvestHasSafeGround(SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino, const AutoHarvestProbeFrame& ProbeFrame,
            const SDK::FVector& Direction, float MaxSafeDropMeters,
            const SDK::TArray<SDK::AActor*>& IgnoreActors,
            SDK::FVector* OutNextFootLocation = nullptr) {
            if (!World || !Dino) return false;
            if (Dino->CanFly() && Dino->bIsFlying) return true;

            // Probe the near and far halves consecutively. Each segment starts
            // from the previously observed ground height, so a long Strider
            // footprint can follow a gentle slope without being mistaken for a
            // single large drop. A real gap still fails at the first missing step.
            const double ProbeDistance = std::clamp(
                static_cast<double>(ProbeFrame.CapsuleRadius) * 1.4 + 125.0,
                250.0, 900.0);
            const double MaxDropUU = PhoenixInterface::FiniteClamp(MaxSafeDropMeters,
                .5f, PhoenixInterface::MaxHarvestSafeDropMeters, 2.5f) * 100.0;
            double PreviousFraction = 0.0;
            double PreviousGroundZ = ProbeFrame.FootLocation.Z;
            constexpr std::array<double, 2> ProbeFractions{ 0.5, 1.0 };
            for (const double Fraction : ProbeFractions) {
                const double SegmentDistance =
                    ProbeDistance * (Fraction - PreviousFraction);
                const double RiseAllowance = std::clamp(std::max(
                    static_cast<double>(ProbeFrame.MaxStepHeight) + 60.0,
                    SegmentDistance * 0.45), 100.0, 350.0);
                SDK::FVector TraceStart{
                    ProbeFrame.FootLocation.X + Direction.X * ProbeDistance * Fraction,
                    ProbeFrame.FootLocation.Y + Direction.Y * ProbeDistance * Fraction,
                    PreviousGroundZ + RiseAllowance
                };
                SDK::FVector TraceEnd = TraceStart;
                TraceEnd.Z = PreviousGroundZ - MaxDropUU;

                SDK::FHitResult Hit{};
                const bool HasGround = SDK::UKismetSystemLibrary::LineTraceSingle(
                    World, TraceStart, TraceEnd, SDK::ETraceTypeQuery::TraceTypeQuery1,
                    false, IgnoreActors, SDK::EDrawDebugTrace::None, &Hit, true,
                    SDK::FLinearColor(0, 0, 0, 0),
                    SDK::FLinearColor(0, 0, 0, 0), 0.0f);
                if (!HasGround || !Hit.bBlockingHit ||
                    !IsFiniteAutoHarvestVector(Hit.ImpactPoint) ||
                    !IsAutoHarvestWalkableHit(Dino, Hit,
                        ProbeFrame.WalkableFloorZ)) {
                    return false;
                }
                PreviousGroundZ = Hit.ImpactPoint.Z;
                PreviousFraction = Fraction;
                if (OutNextFootLocation) *OutNextFootLocation = Hit.ImpactPoint;
            }
            return true;
        }

        bool AutoHarvestDirectionIsBlocked(SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino, const AutoHarvestProbeFrame& ProbeFrame,
            const SDK::FVector& Direction,
            const SDK::FVector& FinalTarget,
            const SDK::TArray<SDK::AActor*>& IgnoreActors,
            SDK::FHitResult* OutBlockingHit = nullptr) {
            if (OutBlockingHit) *OutBlockingHit = {};
            if (!World || !Dino) return true;
            const double ProbeDistance = std::clamp(
                static_cast<double>(ProbeFrame.CapsuleRadius) * 2.0 + 150.0,
                300.0, 1100.0);
            const float ProbeRadius = static_cast<float>(std::clamp(
                static_cast<double>(ProbeFrame.CapsuleRadius) * 0.38,
                35.0, 200.0));
            SDK::FVector TraceStart = ProbeFrame.FootLocation;
            TraceStart.Z += std::max(
                static_cast<double>(ProbeFrame.MaxStepHeight) +
                    static_cast<double>(ProbeRadius) + 25.0,
                static_cast<double>(ProbeRadius) * 1.35);
            SDK::FVector TraceEnd{
                TraceStart.X + Direction.X * ProbeDistance,
                TraceStart.Y + Direction.Y * ProbeDistance,
                TraceStart.Z
            };

            SDK::FHitResult Hit{};
            const bool HasHit = SDK::UKismetSystemLibrary::SphereTraceSingle(
                World, TraceStart, TraceEnd, ProbeRadius,
                SDK::ETraceTypeQuery::TraceTypeQuery1, false, IgnoreActors,
                SDK::EDrawDebugTrace::None, &Hit, true,
                SDK::FLinearColor(0, 0, 0, 0), SDK::FLinearColor(0, 0, 0, 0), 0.0f);
            if (!HasHit || !Hit.bBlockingHit) return false;
            // A sphere that starts against a small uphill surface may report
            // penetration. Honor the mount's own walkable-floor rule before
            // treating that contact as an obstacle.
            if (IsAutoHarvestWalkableHit(Dino, Hit,
                ProbeFrame.WalkableFloorZ)) return false;
            if (Hit.bStartPenetrating) {
                if (OutBlockingHit) *OutBlockingHit = Hit;
                return true;
            }

            // Only ignore a hit very close to the selected node itself. The old
            // attack-distance-sized allowance also ignored cliffs and walls in
            // front of a node, so the mount continuously walked into them.
            const double AllowedTargetDistanceUU = std::clamp(
                static_cast<double>(ProbeFrame.CapsuleRadius) * 0.75 + 150.0,
                250.0, 500.0);
            const bool Blocked = !IsAutoHarvestBreakableObstacle(Hit) ||
                AutoHarvestDistanceSquared(Hit.ImpactPoint, FinalTarget) >
                    AllowedTargetDistanceUU * AllowedTargetDistanceUU;
            if (Blocked && OutBlockingHit) *OutBlockingHit = Hit;
            return Blocked;
        }

        bool IsAutoHarvestBreakableObstacle(const SDK::FHitResult& Hit) {
            SDK::UPrimitiveComponent* Component = Hit.Component.Get();
            SDK::UObject* HitObject = Hit.HitObjectHandle.ReferenceObject.Get();
            SDK::AActor* Actor = Component ? Component->GetOwner() : nullptr;
            if (!Actor && HitObject && IsUsableObject(HitObject) &&
                HitObject->IsA(SDK::AActor::StaticClass())) {
                Actor = static_cast<SDK::AActor*>(HitObject);
            }

            // Never turn obstacle clearing into an attack on player structures.
            if (Actor && IsUsableObject(Actor) &&
                Actor->IsA(SDK::APrimalStructure::StaticClass())) {
                return false;
            }

            if (Actor && IsUsableObject(Actor) &&
                Actor->IsA(SDK::APrimalTargetableActor::StaticClass())) {
                SDK::APrimalTargetableActor* Targetable =
                    static_cast<SDK::APrimalTargetableActor*>(Actor);
                if (Targetable->MyHarvestingComponent) return true;
            }

            if (Component && IsUsableObject(Component) &&
                Component->IsA(SDK::UInstancedStaticMeshComponent::StaticClass())) {
                SDK::UInstancedStaticMeshComponent* Instanced =
                    static_cast<SDK::UInstancedStaticMeshComponent*>(Component);
                if (Instanced->AttachedComponentClass) return true;
            }

            std::string Identity;
            auto AppendIdentity = [&](SDK::UObject* Object) {
                if (!Object || !IsUsableObject(Object)) return;
                Identity += Object->GetFullName();
                Identity += ' ';
                Identity += Object->GetName();
                Identity += ' ';
                if (Object->Class) Identity += Object->Class->GetName();
                Identity += ' ';
            };
            AppendIdentity(Component);
            AppendIdentity(Actor);
            AppendIdentity(HitObject);
            std::transform(Identity.begin(), Identity.end(), Identity.begin(),
                [](unsigned char Character) {
                    return static_cast<char>(std::tolower(Character));
                });
            constexpr std::array<const char*, 8> HarvestableNames{
                "harvest", "foliage", "tree", "rock", "boulder", "stone",
                "stump", "driftwood"
            };
            return std::any_of(HarvestableNames.begin(), HarvestableNames.end(),
                [&](const char* Name) {
                    return Identity.find(Name) != std::string::npos;
                });
        }

        struct AutoHarvestSafeDirectionResult {
            bool Found = false;
            bool Avoiding = false;
            bool CliffDetected = false;
            SDK::FVector Direction{};
        };

        AutoHarvestSafeDirectionResult FindAutoHarvestSafeDirection(
            SDK::UWorld* World, SDK::APrimalDinoCharacter* Dino,
            const AutoHarvestProbeFrame& ProbeFrame,
            const SDK::FVector& BaseDirection, const SDK::FVector& FinalTarget,
            float MaxSafeDropMeters,
            const std::chrono::steady_clock::time_point Now,
            const SDK::TArray<SDK::AActor*>& IgnoreActors,
            bool ForceDetour = false, bool PreferRight = false) {
            AutoHarvestSafeDirectionResult Result{};

            auto IsDirectionSafe = [&](const SDK::FVector& Direction, bool* OutCliff,
                AutoHarvestProbeFrame* OutNext = nullptr) {
                SDK::FVector NextFoot{};
                if (!AutoHarvestHasSafeGround(World, Dino, ProbeFrame, Direction,
                    MaxSafeDropMeters, IgnoreActors, &NextFoot)) {
                    if (OutCliff) *OutCliff = true;
                    return false;
                }
                if (AutoHarvestDirectionIsBlocked(World, Dino, ProbeFrame,
                    Direction, FinalTarget, IgnoreActors)) return false;
                if (OutNext) {
                    *OutNext = ProbeFrame;
                    OutNext->CollisionCenter = OutNext->CollisionCenter + (NextFoot - ProbeFrame.FootLocation);
                    OutNext->FootLocation = NextFoot;
                }
                return true;
            };

            if (Now < s_autoHarvestState.AvoidUntil &&
                IsFiniteAutoHarvestVector(s_autoHarvestState.AvoidDirection)) {
                bool Cliff = false;
                if (IsDirectionSafe(s_autoHarvestState.AvoidDirection, &Cliff)) {
                    Result.Found = true;
                    Result.Avoiding = true;
                    Result.CliffDetected = Cliff;
                    Result.Direction = s_autoHarvestState.AvoidDirection;
                    return Result;
                }
                s_autoHarvestState.AvoidUntil = {};
            }

            const std::array<float, 7> SteeringAngles = ForceDetour
                ? (PreferRight
                    ? std::array<float, 7>{ 72.0f, -72.0f, 105.0f, -105.0f, 140.0f, -140.0f, 180.0f }
                    : std::array<float, 7>{ -72.0f, 72.0f, -105.0f, 105.0f, -140.0f, 140.0f, 180.0f })
                : std::array<float, 7>{ 0.0f, -32.0f, 32.0f, -62.0f, 62.0f, -100.0f, 100.0f };
            size_t FirstSide = 0;
            if (!ForceDetour) {
                bool Cliff = false;
                if (IsDirectionSafe(BaseDirection, &Cliff)) {
                    s_autoHarvestState.SteeringPairCursor = 0;
                    Result.Found = true; Result.Direction = BaseDirection;
                    return Result; // The clear direct route needs no side probes.
                }
                Result.CliffDetected = Cliff;
                FirstSide = 1;
            }
            SDK::FVector TowardTarget{};
            if (!MakeHorizontalAutoHarvestDirection(ProbeFrame.CollisionCenter, FinalTarget, &TowardTarget))
                TowardTarget = BaseDirection;
            // Compare paired left/right detours at each angle, with one extra
            // ground segment of lookahead. Do not blindly choose the first side
            // when it leads straight into another wall or a missing floor.
            // Try one symmetric pair per update. If both fail, the next update
            // explores wider angles; never cast every side probe in one frame.
            const size_t PairCount = (SteeringAngles.size() - FirstSide + 1) / 2;
            const size_t NextPair = FirstSide + 2 * (s_autoHarvestState.SteeringPairCursor++ % PairCount);
            {
                const size_t Pair = NextPair;
                Harvest::DetourCost BestCost{};
                bool Found = false;
                for (size_t Index = Pair; Index < std::min(Pair + 2, SteeringAngles.size()); ++Index) {
                    const float Angle = SteeringAngles[Index];
                    const auto Candidate = RotateAutoHarvestDirection(BaseDirection, Angle);
                    AutoHarvestProbeFrame NextFrame{};
                    if (!IsDirectionSafe(Candidate, nullptr, &NextFrame)) continue;
                    const bool ContinuationClear = AutoHarvestHasSafeGround(World, Dino, NextFrame,
                        Candidate, MaxSafeDropMeters, IgnoreActors) &&
                        !AutoHarvestDirectionIsBlocked(World, Dino, NextFrame, Candidate, FinalTarget, IgnoreActors);
                    const Harvest::DetourCost Cost{ContinuationClear,
                        Candidate.X * TowardTarget.X + Candidate.Y * TowardTarget.Y,
                        PreferRight ? Angle > 0 : Angle < 0};
                    if (Found && !Harvest::PreferDetour(Cost, BestCost)) continue;
                    BestCost = Cost; Found = true;
                    Result.Direction = Candidate;
                }
                if (!Found) return Result;
                Result.Found = true; Result.Avoiding = true;
                s_autoHarvestState.AvoidDirection = Result.Direction;
                s_autoHarvestState.AvoidUntil = Now + std::chrono::milliseconds(ForceDetour ? 1600 : 700);
                return Result;
            }
        }

#include "HarvestEfficiency.inl"

        void ClearAutoHarvestTarget() {
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            ClearAutoHarvestViewLock();
            s_autoHarvestState.HasTarget = false;
            s_autoHarvestState.NextTargetSearch = {};
            s_autoHarvestState.NextTargetResolve = {};
            ResetAutoHarvestSelection();
            s_autoHarvestState.RangeRefinements = 0;s_autoHarvestState.LastRangeAdjustment = {};
            s_autoHarvestState.MeleeApproachLimitMeters = std::numeric_limits<float>::infinity();
            s_autoHarvestState.BeamApproachLimitMeters = std::numeric_limits<float>::infinity();
            s_autoHarvestState.TargetLocation = {};
            s_autoHarvestState.TargetType.clear();
            s_autoHarvestState.TargetLastSeen = {};
            s_autoHarvestState.TargetAcquired = {};
            s_autoHarvestState.AttackStarted = {};
            s_autoHarvestState.DesiredMountFacingRotation = {};
            s_autoHarvestState.HasDesiredMountFacingRotation = false;
            s_autoHarvestState.NavigationPoints.clear();
            s_autoHarvestState.NavigationRemaining.clear();
            s_autoHarvestState.NavigationCandidateCursor = 0;
            s_autoHarvestState.NextPathSmoothing = {};
            s_autoHarvestState.NavigationPointIndex = 0;
            s_autoHarvestState.NavigationPathAvailable = false;
            s_autoHarvestState.NavigationPathPartial = false;
            s_autoHarvestState.NextPathRefresh = {};
            s_autoHarvestState.AvoidDirection = {};
            s_autoHarvestState.SteeringPairCursor = 0;
            s_autoHarvestState.AvoidUntil = {};
            s_autoHarvestState.UnsafeSince = {};
            s_autoHarvestState.MovementSampleLocation = {};
            s_autoHarvestState.MovementSampleTime = {};
            s_autoHarvestState.StationarySince = {};
            s_autoHarvestState.NextRecoveryAttempt = {};
            s_autoHarvestState.RecoveryAttempts = 0;
            s_autoHarvestState.LastApproachProgress = {};
            s_autoHarvestState.LayerMismatchSince = {};
            s_autoHarvestState.ObstacleClearUntil = {};
            s_autoHarvestState.NextObstacleAttack = {};
            s_autoHarvestState.ObstacleClearCooldownUntil = {};
            s_autoHarvestState.ObstacleClearAttacksRemaining = 0;
            s_autoHarvestState.ObstacleClearBursts = 0;
            s_autoHarvestState.NextHarvestProgressSample = {};
            s_autoHarvestState.LastHarvestProgress = {};
            s_autoHarvestState.InventoryProgress = {};
            s_autoHarvestState.BestApproachDistanceUU =
                std::numeric_limits<double>::infinity();
            s_autoHarvestState.ApproachProgressUsesNavigation = false;
        }
    }

    std::string GetAutoHarvestStatus() {
        std::scoped_lock Lock(s_autoHarvestStatusMutex);
        return s_autoHarvestStatus;
    }

    void MaintainAutoHarvestCameraAim(
        SDK::AShooterPlayerCameraManager* CameraManager) {
        AutoHarvestViewLockSnapshot Snapshot{};
        {
            std::scoped_lock Lock(s_autoHarvestViewLockMutex);
            Snapshot = s_autoHarvestViewLock;
        }
        if (!CameraManager || !g_Config::bAutoHarvestResources ||
            !g_Config::bAutoHarvestTekStrider ||
            !Snapshot.Active ||
            !IsFiniteAutoHarvestVector(Snapshot.TargetLocation) ||
            g_MDX12::g_MenuState::g_isOpen) {
            return;
        }

        const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
        if (!GameWindow || GetForegroundWindow() != GameWindow) return;

        SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
        if (!IsUsableObject(LocalPC) || LocalPC->PlayerCameraManager != CameraManager ||
            LocalPC->bShowMouseCursor || !IsUsableObject(LocalPC->Pawn) ||
            !LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            return;
        }

        auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(LocalPC->Pawn);
        int32_t LiveDinoSerialNumber = -1;
        if (!IsUsableObject(Dino) || !Dino->IsLocallyControlled() ||
            Dino->Controller != LocalPC || !Dino->Rider.Get() ||
            !IsTekStrider(Dino) || Dino != Snapshot.Dino ||
            Snapshot.DinoObjectIndex < 0 ||
            Dino->Index != Snapshot.DinoObjectIndex ||
            !TryGetLiveObjectSerial(Dino, Snapshot.DinoObjectIndex,
                &LiveDinoSerialNumber) ||
            LiveDinoSerialNumber != Snapshot.DinoSerialNumber) {
            return;
        }

        const SDK::FVector CameraLocation = CameraManager->GetCameraLocation();
        if (!IsFiniteAutoHarvestVector(CameraLocation)) return;

        SDK::FRotator ViewRotation = SDK::UKismetMathLibrary::FindLookAtRotation(
            CameraLocation, Snapshot.TargetLocation);
        ViewRotation.Pitch = std::clamp(ViewRotation.Pitch, -89.0, 89.0);
        ViewRotation.Roll = 0.0;
        LocalPC->SetControlRotation(ViewRotation);
    }

    bool ResetAutoHarvestForUnload() {
        using Clock = std::chrono::steady_clock;
        ClearAutoHarvestViewLock();
        ResetAutoHarvestMeleeReach();
        ReleaseAutoHarvestAttack();
        ReleaseAutoHarvestSprint();
        RestoreAutoHarvestAquaticPitchControl();
        if (s_autoHarvestState.OwnsForceTurn) g_Config::bForceTurn = false;
        g_Config::bAutoHarvestResources = false;

        if (s_autoHarvestState.TekStriderBeamStartRequested) {
            if (Clock::now() < s_autoHarvestState.NextTekStriderBeamToggle) {
                return false;
            }

            SDK::APrimalDinoCharacter* BeamDino = nullptr;
            int32_t LiveSerialNumber = -1;
            const int32_t BeamDinoIndex =
                s_autoHarvestState.TekStriderBeamDinoObjectIndex;
            if (BeamDinoIndex >= 0 && SDK::UObject::GObjects.GetTypedPtr() &&
                BeamDinoIndex < SDK::UObject::GObjects->Num()) {
                SDK::UObject* CurrentObject =
                    SDK::UObject::GObjects->GetByIndex(BeamDinoIndex);
                if (CurrentObject == s_autoHarvestState.TekStriderBeamDino &&
                    TryGetLiveObjectSerial(CurrentObject, BeamDinoIndex,
                        &LiveSerialNumber) &&
                    LiveSerialNumber ==
                        s_autoHarvestState.TekStriderBeamDinoSerialNumber &&
                    IsUsableObject(CurrentObject) &&
                    CurrentObject->IsA(
                        SDK::APrimalDinoCharacter::StaticClass())) {
                    BeamDino = static_cast<SDK::APrimalDinoCharacter*>(
                        CurrentObject);
                }
            }
            SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
            const bool StillControlsBeamDino = BeamDino &&
                IsUsableObject(LocalPC) && IsUsableObject(LocalPC->Pawn) &&
                LocalPC->Pawn == BeamDino && BeamDino->Controller == LocalPC &&
                BeamDino->IsLocallyControlled();
            if (StillControlsBeamDino) {
                // This is the native path used by the pawn's right-click input.
                // It executes synchronously on the UE game thread, so unload
                // does not depend on a queued Windows input surviving teardown.
                BeamDino->OnAltFirePressed();
                BeamDino->OnAltFireReleased();
            }
            ForgetAutoHarvestTekStriderBeamRequest();
        }

        ClearAutoHarvestTarget();
        s_autoHarvestState = AutoHarvestRuntimeState{};
        SetAutoHarvestStatus("未启用");
        return true;
    }

    void AutoHarvestResources(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point Now = Clock::now();
        const bool TekStriderMode = g_Config::bAutoHarvestTekStrider;
        const bool AutoApproachEnabled =
            TekStriderMode || g_Config::bAutoHarvestApproach;

        if (!g_Config::bAutoHarvestResources || IsFreeCameraActive()) {
            ResetAutoHarvestMeleeReach();
            ClearAutoHarvestViewLock();
            if (s_autoHarvestState.AttackHeld) ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            if (s_autoHarvestState.OwnsForceTurn) g_Config::bForceTurn = false;
            RestoreAutoHarvestAquaticPitchControl();
            if (!StopAutoHarvestTekStriderBeamIfOwned()) {
                SetAutoHarvestStatus(
                    "自动采集已关闭；回到游戏并关闭菜单后将停止跨步者采集光束");
                return;
            }
            if (s_autoHarvestState.WasEnabled) SetAutoHarvestStatus(IsFreeCameraActive() ? "自由视角期间暂停采集" : "未启用");
            s_autoHarvestState = AutoHarvestRuntimeState{};
            return;
        }

        if (!s_autoHarvestState.WasEnabled) {
            s_autoHarvestState.WasEnabled = true;
            s_autoHarvestState.TekStriderMode = TekStriderMode;
            s_autoHarvestState.NextUpdate = Clock::time_point::min();
            s_autoHarvestState.NextAttack = Now + std::chrono::milliseconds(250);
        }
        else if (s_autoHarvestState.TekStriderMode != TekStriderMode) {
            if (s_autoHarvestState.TekStriderMode &&
                !StopAutoHarvestTekStriderBeamIfOwned()) {
                SetAutoHarvestStatus(
                    "等待回到游戏并关闭菜单，以停止跨步者采集光束并切换模式");
                return;
            }
            ClearAutoHarvestTarget();
            s_autoHarvestState.TekStriderMode = TekStriderMode;
        }
        if (!g_Config::bForceTurn || IsFreeCameraActive()) {
            g_Config::bForceTurn = true;
            s_autoHarvestState.OwnsForceTurn = true;
        }

        // Camera updates and the viewport fallback share serialized scheduling.
        // Limit automation work to 30 ms intervals while releasing held clicks promptly.
        if (Now < s_autoHarvestState.NextUpdate) return;
        s_autoHarvestState.NextUpdate = Now + std::chrono::milliseconds(30);

        if (s_autoHarvestState.AttackHeld && Now >= s_autoHarvestState.ReleaseAttack) {
            ReleaseAutoHarvestAttack();
        }
        if (!g_Config::bAutoHarvestBreakObstacles &&
            s_autoHarvestState.ObstacleClearUntil != Clock::time_point{}) {
            ReleaseAutoHarvestAttack();
            s_autoHarvestState.ObstacleClearUntil = {};
            s_autoHarvestState.NextObstacleAttack = {};
            s_autoHarvestState.ObstacleClearAttacksRemaining = 0;
        }

        if (g_MDX12::g_MenuState::g_isOpen) {
            ResetAutoHarvestMeleeReach();
            ClearAutoHarvestTarget();RestoreAutoHarvestAquaticPitchControl();
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            SetAutoHarvestStatus(s_autoHarvestState.TekStriderBeamStartRequested
                ? "菜单打开，已暂停移动；跨步者采集光束保持开启"
                : "菜单打开，已暂停");
            return;
        }
        const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
        if (!GameWindow || GetForegroundWindow() != GameWindow) {
            ResetAutoHarvestMeleeReach();
            ClearAutoHarvestTarget();RestoreAutoHarvestAquaticPitchControl();
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            SetAutoHarvestStatus(s_autoHarvestState.TekStriderBeamStartRequested
                ? "游戏未处于前台，已暂停移动；跨步者采集光束保持开启"
                : "游戏未处于前台，已暂停");
            return;
        }

        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() <= 0) {
            ResetAutoHarvestMeleeReach();
            ReleaseAutoHarvestAttack();
            RestoreAutoHarvestAquaticPitchControl();
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("等待进入世界");
            return;
        }

        SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
        if (!IsUsableObject(LocalPC) || !LocalPC->Pawn ||
            !LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            ResetAutoHarvestMeleeReach();
            ReleaseAutoHarvestAttack();
            RestoreAutoHarvestAquaticPitchControl();
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("需要骑乘可采集资源的生物");
            return;
        }
        if (LocalPC->bShowMouseCursor || !PhoenixShortcuts::GameplayInputReady(LocalPC, false, false)) {
            ResetAutoHarvestMeleeReach();
            ClearAutoHarvestTarget();
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            RestoreAutoHarvestAquaticPitchControl();
            SetAutoHarvestStatus("游戏界面或背包已打开，自动采集暂停");
            return;
        }

        SDK::APrimalDinoCharacter* Dino =
            static_cast<SDK::APrimalDinoCharacter*>(LocalPC->Pawn);
        if (!IsUsableObject(Dino) || Dino->IsDead() || !Dino->Rider.Get() || !Dino->IsLocallyControlled() || Dino->Controller != LocalPC) {
            ResetAutoHarvestMeleeReach();
            ReleaseAutoHarvestAttack();
            RestoreAutoHarvestAquaticPitchControl();
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("需要骑乘并控制存活的生物");
            return;
        }

        int32_t WorldSerial = -1, DinoSerial = -1;
        if (!TryGetLiveObjectSerial(World, World->Index, &WorldSerial) ||
            !TryGetLiveObjectSerial(Dino, Dino->Index, &DinoSerial)) {
            ResetAutoHarvestMeleeReach();ClearAutoHarvestTarget();RestoreAutoHarvestAquaticPitchControl();return;
        }
        if (s_autoHarvestState.ControlledDino && (s_autoHarvestState.ControlledDino != Dino ||
            s_autoHarvestState.DinoIndex != Dino->Index || s_autoHarvestState.DinoSerial != DinoSerial ||
            s_autoHarvestState.World != World || s_autoHarvestState.WorldIndex != World->Index || s_autoHarvestState.WorldSerial != WorldSerial)) {
            ResetAutoHarvestMeleeReach();RestoreAutoHarvestAquaticPitchControl();ClearAutoHarvestTarget();
            StopAutoHarvestTekStriderBeamIfOwned();
            s_autoHarvestState.SkippedTargets.clear();s_autoHarvestState.NextProbeGeometry = {};
            s_autoHarvestState.EmptySearches = 0;s_autoHarvestState.AquaticSettleUntil = {};
        }
        s_autoHarvestState.World = World;s_autoHarvestState.WorldIndex = World->Index;s_autoHarvestState.WorldSerial = WorldSerial;
        s_autoHarvestState.ControlledDino = Dino;s_autoHarvestState.DinoIndex = Dino->Index;s_autoHarvestState.DinoSerial = DinoSerial;
        const float RangeMultiplier = PhoenixInterface::FiniteClamp(
            g_Config::DinoHarvestRangeMultiplier, 1.f, PhoenixInterface::MaxDinoHarvestRangeMultiplier, 2.f);
        if (TekStriderMode) ResetAutoHarvestMeleeReach();
        else if (Now >= s_autoHarvestState.NextMeleeReach || !s_autoHarvestState.MeleeReachReady ||
            s_autoHarvestState.CachedRangeEnabled != g_Config::bDinoHarvestRange ||
            s_autoHarvestState.CachedRangeMultiplier != RangeMultiplier) {
            UpdateDinoHarvestRange(Dino, g_Config::bDinoHarvestRange, RangeMultiplier, GetTickCount64());
            s_autoHarvestState.NativeMeleeReachMeters = 0;
            GetDinoHarvestReachMeters(Dino, s_autoHarvestState.NativeMeleeReachMeters);
            s_autoHarvestState.MeleeReachReady = true;s_autoHarvestState.CachedRangeEnabled = g_Config::bDinoHarvestRange;
            s_autoHarvestState.CachedRangeMultiplier = RangeMultiplier;
            s_autoHarvestState.NextMeleeReach = Now + std::chrono::milliseconds(250);
        }

        if (TekStriderMode && !IsTekStrider(Dino)) {
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("跨步者采集模式需要骑乘跨步者");
            return;
        }
        if (TekStriderMode && !HasTekStriderExcavationRig(Dino)) {
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("当前跨步者没有装备采集钻头头部配件");
            return;
        }
        if (TekStriderMode && s_autoHarvestState.TekStriderBeamStartRequested) {
            int32_t CurrentDinoSerialNumber = -1;
            const bool SameBeamOwner =
                s_autoHarvestState.TekStriderBeamDino == Dino &&
                s_autoHarvestState.TekStriderBeamDinoObjectIndex == Dino->Index &&
                TryGetLiveObjectSerial(Dino, Dino->Index,
                    &CurrentDinoSerialNumber) &&
                s_autoHarvestState.TekStriderBeamDinoSerialNumber ==
                    CurrentDinoSerialNumber;
            if (!SameBeamOwner) {
                // A persistent toggle belongs to the exact mount on which it
                // was sent. A newly controlled Strider gets one fresh request.
                ForgetAutoHarvestTekStriderBeamRequest();
            }
        }

        const SDK::FVector DinoLocation = Dino->K2_GetActorLocation();
        const float SearchRadius = PhoenixInterface::FiniteClamp(g_Config::AutoHarvestSearchRadiusMeters, 5.f, PhoenixInterface::MaxHarvestSearchMeters, 30.f);
        const auto CurrentProbeFrame = ResolveAutoHarvestProbeFrame(Dino, DinoLocation);
        float AttackDistance = TekStriderMode
            ? std::min(PhoenixInterface::FiniteClamp(g_Config::AutoHarvestTekStriderBeamRangeMeters, 10.f,
                PhoenixInterface::MaxHarvestBeamMeters, 35.f), s_autoHarvestState.BeamApproachLimitMeters)
            : std::min(Harvest::MeleeApproachRange(g_Config::AutoHarvestAttackDistanceMeters,
                s_autoHarvestState.NativeMeleeReachMeters, CurrentProbeFrame.CapsuleRadius * .01f), s_autoHarvestState.MeleeApproachLimitMeters);
        const int ConfiguredAttackIntervalMs = int(PhoenixInterface::FiniteClamp(g_Config::AutoHarvestAttackIntervalMs,
            200.f, PhoenixInterface::MaxHarvestAttackIntervalMs, 850.f));
        const bool IsAerialCreature = Dino->CanFly() && Dino->bIsFlying;
        // Movement-mode-only checks miss several dedicated water-dino
        // blueprints (notably Dunkleosteus) while mounted. Use the species
        // capability first so they can never enter the land nav/obstacle path.
        const bool IsAquaticCreature = Dino->IsWaterDino() || Dino->LivesInWater() ||
            Dino->IsPrimalCharSwimming() ||
            Dino->bSwimmingWaterDinoMoveLikeFlying ||
            (Dino->CharacterMovement && IsUsableObject(Dino->CharacterMovement) &&
                Dino->CharacterMovement->IsSwimming());
        if (IsAquaticCreature) {
            ApplyAutoHarvestAquaticPitchControl(Dino);
        }
        else {
            RestoreAutoHarvestAquaticPitchControl();
        }

        auto GetAquaticSpeed = [&]() {
            if (!Dino->CharacterMovement || !IsUsableObject(Dino->CharacterMovement) ||
                !IsFiniteAutoHarvestVector(Dino->CharacterMovement->Velocity)) {
                return 0.0;
            }
            const SDK::FVector& Velocity = Dino->CharacterMovement->Velocity;
            return std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y +
                Velocity.Z * Velocity.Z);
        };

        if (s_autoHarvestState.HasTarget && Now >= s_autoHarvestState.NextTargetResolve) {
            s_autoHarvestState.NextTargetResolve = Now + std::chrono::milliseconds(IsAquaticCreature ? 150 : 180);
            SDK::FVector ResolvedLocation{};
            std::string ResolvedType;
            // Underwater metal clusters often contain many instances only a
            // metre or two apart. A wide resolve radius silently transferred
            // the lock from one node to its neighbour every cache refresh,
            // making a swimming mount weave between both. Aquatic locks only
            // accept the same physical instance's immediate position.
            const float ResolveRadiusMeters = IsAquaticCreature ? 0.65f : 1.75f;
            if (g_DrawESP::FindResourceTargetNear(s_autoHarvestState.TargetLocation,
                ResolveRadiusMeters,
                &ResolvedLocation, &ResolvedType)) {
                s_autoHarvestState.TargetLocation = ResolvedLocation;
                s_autoHarvestState.TargetType = std::move(ResolvedType);
                s_autoHarvestState.TargetLastSeen = Now;
            }
            else {
                // The resource marker cache is capped and its instanced-component
                // scan order can change between refreshes. A temporary cache miss
                // while travelling must not be treated as a destroyed node, or a
                // newly closer marker makes the mount turn back and forth forever.
                // Keep the acquired location locked until we are close enough to
                // verify that the original node is actually gone.
                const double LockedDistanceSquared = AutoHarvestDistanceSquared(
                    DinoLocation, s_autoHarvestState.TargetLocation);
                const double ConfirmDistanceUU =
                    static_cast<double>(AttackDistance + 2.0f) * 100.0;
                const bool CloseEnoughToConfirmGone = std::isfinite(LockedDistanceSquared) &&
                    LockedDistanceSquared <= ConfirmDistanceUU * ConfirmDistanceUU;
                const bool MissingLongEnough =
                    s_autoHarvestState.TargetLastSeen != Clock::time_point{} &&
                    Now - s_autoHarvestState.TargetLastSeen > std::chrono::milliseconds(
                        IsAquaticCreature ? 3200 : 1800);
                if (CloseEnoughToConfirmGone && MissingLongEnough) {
                    if (IsAquaticCreature) {
                        AddAutoHarvestSkippedTarget(s_autoHarvestState.TargetLocation,
                            Now + std::chrono::seconds(8));
                        // A swimming mount still carries momentum after the node is
                        // destroyed. Do not immediately select a node behind it and
                        // cause a ping-pong turn between two nearby resources.
                        s_autoHarvestState.AquaticSettleUntil =
                            Now + std::chrono::milliseconds(2400);
                    }
                    ClearAutoHarvestTarget();
                }
            }
        }

        // An unreachable node must not hold the automation forever. Skip it for
        // a few seconds, then let the nearest-target selector choose another node.
        if (s_autoHarvestState.HasTarget) {
            if (s_autoHarvestState.AttackStarted != Clock::time_point{})
                SampleAutoHarvestProgress(Dino, Now);
            const bool ApproachTimedOut = s_autoHarvestState.AttackStarted == Clock::time_point{} &&
                Harvest::IdleExpired(Now, s_autoHarvestState.TargetAcquired,
                    s_autoHarvestState.LastApproachProgress,
                    std::chrono::seconds(IsAquaticCreature ? 45 : 30));
            if (!TekStriderMode && s_autoHarvestState.AttackStarted != Clock::time_point{} &&
                s_autoHarvestState.RangeRefinements < 5 &&
                Now - std::max({s_autoHarvestState.AttackStarted, s_autoHarvestState.LastHarvestProgress,
                    s_autoHarvestState.LastRangeAdjustment}) > Harvest::MeleeFeedbackDelay(ConfiguredAttackIntervalMs)) {
                const float Closer = Harvest::RefineMeleeRange(AttackDistance, CurrentProbeFrame.CapsuleRadius * .01f);
                if (Closer + .15f < AttackDistance && AutoApproachEnabled) {
                    s_autoHarvestState.MeleeApproachLimitMeters = AttackDistance = Closer;
                    ++s_autoHarvestState.RangeRefinements;s_autoHarvestState.LastRangeAdjustment = Now;
                    s_autoHarvestState.AttackStarted = {};s_autoHarvestState.NextPathRefresh = {};
                    ReleaseAutoHarvestAttack();
                }
            }
            if (TekStriderMode && s_autoHarvestState.AttackStarted != Clock::time_point{} &&
                s_autoHarvestState.RangeRefinements < 4 &&
                Now - std::max({s_autoHarvestState.AttackStarted, s_autoHarvestState.LastHarvestProgress,
                    s_autoHarvestState.LastRangeAdjustment}) > std::chrono::seconds(3)) {
                const float Closer = Harvest::RefineBeamRange(AttackDistance);
                if (Closer + .5f < AttackDistance) {
                    s_autoHarvestState.BeamApproachLimitMeters = AttackDistance = Closer;
                    ++s_autoHarvestState.RangeRefinements;s_autoHarvestState.LastRangeAdjustment = Now;
                    s_autoHarvestState.AttackStarted = {};s_autoHarvestState.NextPathRefresh = {};
                }
            }
            const bool AttackTimedOut = !TekStriderMode && Harvest::IdleExpired(Now,
                s_autoHarvestState.AttackStarted, s_autoHarvestState.LastHarvestProgress,
                Harvest::MeleeIdleLimit(ConfiguredAttackIntervalMs, IsAquaticCreature));
            const bool TekStriderBeamTimedOut = TekStriderMode && Harvest::IdleExpired(Now,
                s_autoHarvestState.AttackStarted, s_autoHarvestState.LastHarvestProgress,
                std::chrono::seconds(14));
            if (ApproachTimedOut || AttackTimedOut || TekStriderBeamTimedOut) {
                AddAutoHarvestSkippedTarget(s_autoHarvestState.TargetLocation,
                    Now + std::chrono::seconds(12));
                if (IsAquaticCreature) {
                    s_autoHarvestState.AquaticSettleUntil =
                        Now + std::chrono::milliseconds(2800);
                }
                ClearAutoHarvestTarget();
            }
        }

        if (!s_autoHarvestState.HasTarget) {
            if (IsAquaticCreature && AutoApproachEnabled &&
                Now < s_autoHarvestState.AquaticSettleUntil) {
                const double Speed = GetAquaticSpeed();
                if (Speed > 35.0 && Dino->CharacterMovement) {
                    const SDK::FVector& Velocity = Dino->CharacterMovement->Velocity;
                    const SDK::FVector BrakeDirection{
                        -Velocity.X / Speed, -Velocity.Y / Speed, -Velocity.Z / Speed
                    };
                    Dino->AddMovementInput(BrakeDirection,
                        static_cast<float>(std::clamp(Speed / 650.0, 0.25, 1.0)), true);
                }
                ReleaseAutoHarvestAttack();
                SetAutoHarvestStatus("水下惯性减速中，稳定后再锁定下一个资源");
                return;
            }

            if (Now < s_autoHarvestState.NextTargetSearch &&
                (s_autoHarvestState.EmptySearches == 0 || AutoHarvestDistanceSquared(DinoLocation, s_autoHarvestState.SelectionOrigin) < 400. * 400.)) return;
            s_autoHarvestState.NextTargetSearch = Now + std::chrono::milliseconds(250);
            SDK::FVector TargetLocation{};
            float TargetDistance = 0.0f;
            std::string TargetType;
            std::vector<SDK::FVector> ExcludedLocations =
                GetActiveAutoHarvestSkippedLocations(Now);
            bool FoundTarget = false;
            const double AquaticSpeed = IsAquaticCreature ? GetAquaticSpeed() : 0.0;
            SDK::FVector AquaticForward{};
            bool RequireForwardTarget = false;
            if (IsAquaticCreature && AquaticSpeed > 90.0 &&
                Dino->CharacterMovement &&
                IsFiniteAutoHarvestVector(Dino->CharacterMovement->Velocity)) {
                const SDK::FVector& Velocity = Dino->CharacterMovement->Velocity;
                AquaticForward = SDK::FVector{
                    Velocity.X / AquaticSpeed,
                    Velocity.Y / AquaticSpeed,
                    Velocity.Z / AquaticSpeed
                };
                RequireForwardTarget = true;
            }

            const bool RankGroundRoutes = !IsAquaticCreature && !IsAerialCreature && AutoApproachEnabled &&
                (g_Config::bAutoHarvestSafeNavigation || g_Config::bAutoHarvestExperimentalNavigation);
            std::vector<SDK::FVector> SelectedRoute;
            if (RankGroundRoutes) {
                if (!s_autoHarvestState.SelectionCandidates.empty() &&
                    (AutoHarvestDistanceSquared(DinoLocation, s_autoHarvestState.SelectionOrigin) > 200. * 200. ||
                        s_autoHarvestState.SelectionRadius != SearchRadius || s_autoHarvestState.SelectionRange != AttackDistance))
                    ResetAutoHarvestSelection();
                if (s_autoHarvestState.SelectionCandidates.empty()) {
                    s_autoHarvestState.SelectionCandidates = g_DrawESP::FindResourceTargetCandidates(
                        DinoLocation, SearchRadius, 6, ExcludedLocations, 3.0f);
                    s_autoHarvestState.SelectionOrigin = DinoLocation;
                    s_autoHarvestState.SelectionRadius = SearchRadius;
                    s_autoHarvestState.SelectionRange = AttackDistance;
                }
                const auto Frame = ResolveAutoHarvestProbeFrame(Dino, DinoLocation);
                std::array<SDK::AActor*, 128> Storage{};
                SDK::TArray<SDK::AActor*> Ignore(Storage.data(), 0, int32_t(Storage.size()));
                PopulateAutoHarvestTraceIgnoreActors(Dino, Ignore);
                if (!AdvanceAutoHarvestRouteSelection(World, Dino, Frame, DinoLocation, AttackDistance, Ignore)) {
                    s_autoHarvestState.NextTargetSearch = Now + std::chrono::milliseconds(30);
                    ReleaseAutoHarvestAttack();ReleaseAutoHarvestSprint();
                    SetAutoHarvestStatus("正在分步比较附近资源的可达路线");
                    return;
                }
                if (s_autoHarvestState.SelectionHasBest) {
                    FoundTarget = g_DrawESP::FindResourceTargetNear(s_autoHarvestState.SelectionBest.Location,
                        .8f, &TargetLocation, &TargetType);
                    if (FoundTarget) {
                        TargetDistance = float(std::sqrt(AutoHarvestDistanceSquared(DinoLocation, TargetLocation)) * .01);
                        SelectedRoute = std::move(s_autoHarvestState.SelectionBestRoute);
                    }
                }
                ResetAutoHarvestSelection();
            } else {
                ResetAutoHarvestSelection();
                const auto Candidates = g_DrawESP::FindResourceTargetCandidates(DinoLocation, SearchRadius,
                    RequireForwardTarget ? 10 : 1, ExcludedLocations, 3.0f,
                    RequireForwardTarget ? &AquaticForward : nullptr);
                if (!Candidates.empty()) {
                    FoundTarget = true;TargetLocation = Candidates.front().Location;
                    TargetDistance = Candidates.front().DistanceMeters;TargetType = Candidates.front().TypeName;
                }
            }

            if (!FoundTarget && RequireForwardTarget) {
                const SDK::FVector& Velocity = Dino->CharacterMovement->Velocity;
                const SDK::FVector BrakeDirection{
                    -Velocity.X / AquaticSpeed,
                    -Velocity.Y / AquaticSpeed,
                    -Velocity.Z / AquaticSpeed
                };
                Dino->AddMovementInput(BrakeDirection,
                    static_cast<float>(std::clamp(AquaticSpeed / 650.0, 0.30, 1.0)), true);
                s_autoHarvestState.AquaticSettleUntil =
                    Now + std::chrono::milliseconds(450);
                ReleaseAutoHarvestAttack();
                SetAutoHarvestStatus("水下仍有惯性，拒绝掉头追逐身后资源并继续刹车");
                return;
            }

            const auto* ExcludedLocationList = ExcludedLocations.empty()
                ? nullptr : &ExcludedLocations;
            if (!FoundTarget) {
                s_autoHarvestState.SelectionOrigin = DinoLocation;
                s_autoHarvestState.NextTargetSearch = Now + Harvest::EmptySearchDelay(s_autoHarvestState.EmptySearches++);
                ReleaseAutoHarvestAttack();
                SetAutoHarvestStatus(ExcludedLocationList
                    ? "正在跳过无法到达的资源并寻找下一个"
                    : (TekStriderMode &&
                        s_autoHarvestState.TekStriderBeamStartRequested
                        ? "范围内没有已勾选的资源；跨步者采集光束保持开启"
                        : "范围内没有已勾选的资源"));
                return;
            }

            s_autoHarvestState.HasTarget = true;
            s_autoHarvestState.EmptySearches = 0;
            s_autoHarvestState.TargetLocation = TargetLocation;
            s_autoHarvestState.TargetType = std::move(TargetType);
            s_autoHarvestState.TargetLastSeen = Now;
            s_autoHarvestState.TargetAcquired = Now;
            s_autoHarvestState.AttackStarted = {};
            s_autoHarvestState.NextHarvestProgressSample = Now;
            s_autoHarvestState.LastHarvestProgress = {};
            s_autoHarvestState.InventoryProgress = {};
            s_autoHarvestState.NextAttack = Harvest::NextTargetAttack(s_autoHarvestState.NextAttack, Now);
            s_autoHarvestState.NavigationPoints = std::move(SelectedRoute);
            s_autoHarvestState.NavigationRemaining = Harvest::PathSuffix(s_autoHarvestState.NavigationPoints,
                [](const auto& A, const auto& B) { return std::sqrt(AutoHarvestDistanceSquared(A, B)); });
            s_autoHarvestState.NavigationPointIndex = 1;
            s_autoHarvestState.NavigationPathAvailable = s_autoHarvestState.NavigationPoints.size() >= 2;
            s_autoHarvestState.NavigationPathPartial = false;
            s_autoHarvestState.NextPathRefresh = s_autoHarvestState.NavigationPathAvailable
                ? Now + std::chrono::milliseconds(2800) : Clock::time_point::min();
            SampleAutoHarvestProgress(Dino, Now); // Baseline before the first swing/beam contact.
            s_autoHarvestState.MovementSampleLocation = DinoLocation;
            s_autoHarvestState.MovementSampleTime = Now;
            s_autoHarvestState.StationarySince = {};
            s_autoHarvestState.NextRecoveryAttempt = Now;
            s_autoHarvestState.RecoveryAttempts = 0;
            s_autoHarvestState.LastApproachProgress = Now;
            s_autoHarvestState.LayerMismatchSince = {};
            s_autoHarvestState.BestApproachDistanceUU =
                std::numeric_limits<double>::infinity();
            s_autoHarvestState.ApproachProgressUsesNavigation = false;
            s_autoHarvestState.AquaticBrakeUntil = {};
            s_autoHarvestState.AquaticClosestDistanceUU =
                std::sqrt(AutoHarvestDistanceSquared(DinoLocation, TargetLocation));
            s_autoHarvestState.AquaticOvershootCount = 0;
        }

        const double DistanceSquared = AutoHarvestDistanceSquared(
            DinoLocation, s_autoHarvestState.TargetLocation);
        if (!std::isfinite(DistanceSquared)) {
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("资源坐标无效，正在重新锁定");
            return;
        }
        const float DistanceMeters = static_cast<float>(std::sqrt(DistanceSquared) * 0.01);
        if (DistanceMeters > SearchRadius + 2.0f) {
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("目标超出搜索范围，正在锁定下一个");
            return;
        }

        const AutoHarvestProbeFrame ProbeFrame = CurrentProbeFrame;
        const float CapsuleRadius = ProbeFrame.CapsuleRadius;
        const bool IsGroundCreature = !IsAerialCreature && !IsAquaticCreature;
        const bool UseThreeDimensionalMovement = IsAerialCreature || IsAquaticCreature;

        // Visibility and safe steering must ignore the Strider's own body,
        // recursive rig attachments, saddle and rider. Keep one shared list for
        // this update so path candidate checks do not rebuild it repeatedly.
        std::array<SDK::AActor*, 128> TraceIgnoreStorage{};
        SDK::TArray<SDK::AActor*> TraceIgnoreActors(
            TraceIgnoreStorage.data(), 0,
            static_cast<int32_t>(TraceIgnoreStorage.size()));
        if (TekStriderMode || (IsGroundCreature &&
            (g_Config::bAutoHarvestSafeNavigation ||
                g_Config::bAutoHarvestExperimentalNavigation ||
                g_Config::bAutoHarvestBreakObstacles))) {
            PopulateAutoHarvestTraceIgnoreActors(Dino, TraceIgnoreActors);
        }

        SDK::FVector BeamPathOrigin = ProbeFrame.CollisionCenter;
        BeamPathOrigin.Z += std::clamp(
            static_cast<double>(ProbeFrame.CapsuleHalfHeight) * 0.35,
            100.0, 900.0);
        if (TekStriderMode) {
            SDK::FVector CameraLocation{};
            SDK::FRotator CameraRotation{};
            LocalPC->GetPlayerViewPoint(&CameraLocation, &CameraRotation);
            if (IsFiniteAutoHarvestVector(CameraLocation)) {
                BeamPathOrigin = CameraLocation;
            }
        }
        const double BeamTargetHitToleranceUU = std::clamp(
            static_cast<double>(CapsuleRadius) * 0.35, 125.0, 300.0);
        const bool TekStriderTargetVisible = !TekStriderMode ||
            AutoHarvestHasClearBeamPath(World, BeamPathOrigin,
                s_autoHarvestState.TargetLocation, BeamTargetHitToleranceUU,
                TraceIgnoreActors);
        const bool TekStriderTargetOccluded =
            TekStriderMode && !TekStriderTargetVisible;
        const bool NeedsApproach = DistanceMeters > AttackDistance ||
            TekStriderTargetOccluded;

        if (IsGroundCreature) {
            const double DeltaX = s_autoHarvestState.TargetLocation.X - DinoLocation.X;
            const double DeltaY = s_autoHarvestState.TargetLocation.Y - DinoLocation.Y;
            const double HorizontalDistanceUU = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
            const double VerticalDistanceUU = std::abs(
                s_autoHarvestState.TargetLocation.Z - ProbeFrame.FootLocation.Z);
            const double NearbyHorizontalLimitUU = std::max(
                static_cast<double>(AttackDistance) * 100.0 + CapsuleRadius * 3.0, 1200.0);
            const double ReachableVerticalUU = std::max(
                static_cast<double>(AttackDistance) * 100.0 * 0.62,
                static_cast<double>(ProbeFrame.MaxStepHeight) + 75.0);
            if (HorizontalDistanceUU <= NearbyHorizontalLimitUU &&
                VerticalDistanceUU > ReachableVerticalUU) {
                // Do not reject a resource merely because it is currently uphill:
                // a complete nav path whose endpoint reaches the resource layer is
                // valid. A path ending below an overhead shelf is not.
                bool HasReachableLayerPath = false;
                if (g_Config::bAutoHarvestSafeNavigation &&
                    Now >= s_autoHarvestState.NextPathRefresh) {
                    RefreshAutoHarvestNavigationPath(World, Dino, ProbeFrame,
                        DinoLocation, s_autoHarvestState.TargetLocation,
                        AttackDistance, TekStriderTargetOccluded,
                        TraceIgnoreActors);
                    s_autoHarvestState.NextPathRefresh =
                        Now + std::chrono::milliseconds(700);
                }
                if (s_autoHarvestState.NavigationPathAvailable &&
                    !s_autoHarvestState.NavigationPathPartial &&
                    !s_autoHarvestState.NavigationPoints.empty()) {
                    const SDK::FVector& PathEnd =
                        s_autoHarvestState.NavigationPoints.back();
                    const double PathEndDistanceSquared = AutoHarvestDistanceSquared(
                        PathEnd, s_autoHarvestState.TargetLocation);
                    const double PathEndToleranceUU = std::max(
                        static_cast<double>(AttackDistance) * 100.0 +
                            CapsuleRadius * 1.25,
                        600.0);
                    HasReachableLayerPath = std::isfinite(PathEndDistanceSquared) &&
                        PathEndDistanceSquared <=
                            PathEndToleranceUU * PathEndToleranceUU &&
                        std::abs(PathEnd.Z - s_autoHarvestState.TargetLocation.Z) <=
                            ReachableVerticalUU;
                }

                if (!HasReachableLayerPath) {
                    // A short uphill/downhill step can temporarily fail both nav
                    // projection and endpoint checks. Treat it as a suspected
                    // layer mismatch first; actual approach progress below decides
                    // whether the target is truly unreachable.
                    if (s_autoHarvestState.LayerMismatchSince == Clock::time_point{}) {
                        s_autoHarvestState.LayerMismatchSince = Now;
                    }
                }
                else {
                    s_autoHarvestState.LayerMismatchSince = {};
                }
            }
            else {
                s_autoHarvestState.LayerMismatchSince = {};
            }
        }

        SDK::FVector MovementDirection{};
        bool HasMovementDirection = false;
        bool UsingNavigationPath = false;
        bool AvoidingObstacle = false;
        bool ClearingObstacle = false;
        bool AquaticBraking = false;
        bool MovementDirectionControlsFacing = true;
        float MovementInputScale = 1.0f;

        if (NeedsApproach && AutoApproachEnabled) {
            SDK::FVector MovementGoal = s_autoHarvestState.TargetLocation;
            const bool UseSafeNavigation = IsGroundCreature &&
                (TekStriderTargetOccluded ||
                    g_Config::bAutoHarvestSafeNavigation ||
                    g_Config::bAutoHarvestExperimentalNavigation);
            const bool MonitorGroundRecovery = IsGroundCreature &&
                (UseSafeNavigation || g_Config::bAutoHarvestBreakObstacles);
            bool ForceDetour = false;
            if (MonitorGroundRecovery) {
                if (s_autoHarvestState.MovementSampleTime == Clock::time_point{}) {
                    s_autoHarvestState.MovementSampleLocation = DinoLocation;
                    s_autoHarvestState.MovementSampleTime = Now;
                }
                else if (Now - s_autoHarvestState.MovementSampleTime >=
                    std::chrono::milliseconds(700)) {
                    const double MoveX = DinoLocation.X -
                        s_autoHarvestState.MovementSampleLocation.X;
                    const double MoveY = DinoLocation.Y -
                        s_autoHarvestState.MovementSampleLocation.Y;
                    const double HorizontalMovementUU = std::sqrt(MoveX * MoveX + MoveY * MoveY);
                    const double RequiredMovementUU = std::clamp(
                        static_cast<double>(CapsuleRadius) * 0.18, 45.0, 180.0);
                    if (HorizontalMovementUU >= RequiredMovementUU) {
                        s_autoHarvestState.StationarySince = {};
                        s_autoHarvestState.RecoveryAttempts = 0;
                        const double ClearBurstResetDistanceUU = std::max(
                            RequiredMovementUU * 3.0, 250.0);
                        if (HorizontalMovementUU >= ClearBurstResetDistanceUU) {
                            s_autoHarvestState.ObstacleClearBursts = 0;
                        }
                    }
                    else if (s_autoHarvestState.StationarySince == Clock::time_point{}) {
                        s_autoHarvestState.StationarySince = s_autoHarvestState.MovementSampleTime;
                    }
                    s_autoHarvestState.MovementSampleLocation = DinoLocation;
                    s_autoHarvestState.MovementSampleTime = Now;
                }

                if (s_autoHarvestState.StationarySince != Clock::time_point{}) {
                    const auto StationaryDuration = Now - s_autoHarvestState.StationarySince;
                    const bool ClearingInProgress =
                        s_autoHarvestState.ObstacleClearUntil != Clock::time_point{} &&
                        Now < s_autoHarvestState.ObstacleClearUntil;
                    if (StationaryDuration > std::chrono::milliseconds(5500) &&
                        !ClearingInProgress) {
                        AddAutoHarvestSkippedTarget(s_autoHarvestState.TargetLocation,
                            Now + std::chrono::seconds(20));
                        ClearAutoHarvestTarget();
                        SetAutoHarvestStatus("持续贴墙无法脱困，已跳过该资源");
                        return;
                    }
                    ForceDetour = StationaryDuration > std::chrono::milliseconds(1500);
                    if (ForceDetour && Now >= s_autoHarvestState.NextRecoveryAttempt) {
                        s_autoHarvestState.AvoidUntil = {};
                        ++s_autoHarvestState.RecoveryAttempts;
                        s_autoHarvestState.NextPathRefresh = Harvest::BringForward(
                            s_autoHarvestState.NextPathRefresh, Now);
                        s_autoHarvestState.NextRecoveryAttempt =
                            Now + std::chrono::milliseconds(1200);
                    }
                }
            }
            if (UseSafeNavigation) {
                if (Now >= s_autoHarvestState.NextPathRefresh) {
                    RefreshAutoHarvestNavigationPath(World, Dino, ProbeFrame,
                        DinoLocation, s_autoHarvestState.TargetLocation,
                        AttackDistance, TekStriderTargetOccluded,
                        TraceIgnoreActors);
                    s_autoHarvestState.NextPathRefresh = Now + Harvest::ReplanDelay(
                        s_autoHarvestState.NavigationPathAvailable && !s_autoHarvestState.NavigationPathPartial,
                        s_autoHarvestState.LastApproachProgress != Clock::time_point{} &&
                            Now - s_autoHarvestState.LastApproachProgress < std::chrono::seconds(3),
                        ForceDetour || s_autoHarvestState.UnsafeSince != Clock::time_point{},
                        g_Config::bAutoHarvestExperimentalNavigation);
                }
                if (s_autoHarvestState.NavigationPathAvailable &&
                    s_autoHarvestState.NavigationPathPartial &&
                    !s_autoHarvestState.NavigationPoints.empty()) {
                    const SDK::FVector& PathEnd = s_autoHarvestState.NavigationPoints.back();
                    const double PathEndDistanceSquared = AutoHarvestDistanceSquared(
                        PathEnd, s_autoHarvestState.TargetLocation);
                    const double ReachToleranceUU = std::max(
                        static_cast<double>(AttackDistance) * 100.0 + CapsuleRadius * 1.5,
                        650.0);
                    if (std::isfinite(PathEndDistanceSquared) &&
                        PathEndDistanceSquared > ReachToleranceUU * ReachToleranceUU) {
                        // Partial nav paths also occur at short climbable slopes.
                        // Let real movement progress confirm the route instead of
                        // cancelling the target from this projection result alone.
                        if (s_autoHarvestState.LayerMismatchSince == Clock::time_point{}) {
                            s_autoHarvestState.LayerMismatchSince = Now;
                        }
                    }
                }
                MovementGoal = GetAutoHarvestNavigationGoal(DinoLocation,
                    s_autoHarvestState.TargetLocation, CapsuleRadius, World, Dino);
                UsingNavigationPath = s_autoHarvestState.NavigationPathAvailable;
            }

            // Position-only stuck checks miss the common case where a mount keeps
            // moving in circles under an elevated node. Track actual remaining
            // route distance and require meaningful progress toward the resource.
            const double RemainingApproachDistanceUU =
                GetAutoHarvestRemainingPathDistance(
                    DinoLocation, s_autoHarvestState.TargetLocation);
            const bool ProgressUsesNavigation =
                s_autoHarvestState.NavigationPathAvailable;
            const double RequiredProgressUU = std::clamp(
                static_cast<double>(CapsuleRadius) * 0.35, 70.0, 180.0);
            const bool ProgressMetricChanged =
                ProgressUsesNavigation !=
                    s_autoHarvestState.ApproachProgressUsesNavigation;

            if (std::isfinite(RemainingApproachDistanceUU) &&
                (s_autoHarvestState.LastApproachProgress == Clock::time_point{} ||
                    !std::isfinite(s_autoHarvestState.BestApproachDistanceUU) ||
                    ProgressMetricChanged)) {
                s_autoHarvestState.BestApproachDistanceUU =
                    RemainingApproachDistanceUU;
                s_autoHarvestState.LastApproachProgress = Now;
                s_autoHarvestState.ApproachProgressUsesNavigation =
                    ProgressUsesNavigation;
            }
            else if (std::isfinite(RemainingApproachDistanceUU) &&
                RemainingApproachDistanceUU + RequiredProgressUU <
                    s_autoHarvestState.BestApproachDistanceUU) {
                s_autoHarvestState.BestApproachDistanceUU =
                    RemainingApproachDistanceUU;
                s_autoHarvestState.LastApproachProgress = Now;
            }

            const bool SuspectedLayerMismatch =
                s_autoHarvestState.LayerMismatchSince != Clock::time_point{};
            const auto NoProgressTimeout = SuspectedLayerMismatch
                ? std::chrono::milliseconds(4500)
                : std::chrono::milliseconds(7500);
            const bool ClearingInProgress =
                s_autoHarvestState.ObstacleClearUntil != Clock::time_point{} &&
                Now < s_autoHarvestState.ObstacleClearUntil;
            if (s_autoHarvestState.LastApproachProgress != Clock::time_point{} &&
                Now - s_autoHarvestState.LastApproachProgress > NoProgressTimeout &&
                !ClearingInProgress) {
                AddAutoHarvestSkippedTarget(s_autoHarvestState.TargetLocation,
                    Now + std::chrono::seconds(45));
                ClearAutoHarvestTarget();
                SetAutoHarvestStatus(SuspectedLayerMismatch ?
                    "疑似跨层且持续未接近目标，已跳过并寻找同层资源" :
                    "持续绕行但未接近目标，已跳过该资源并重新选点");
                return;
            }

            SDK::FVector BaseDirection{};
            if (UseThreeDimensionalMovement) {
                if (!MakeThreeDimensionalAutoHarvestDirection(DinoLocation,
                    MovementGoal, &BaseDirection)) {
                    MakeThreeDimensionalAutoHarvestDirection(DinoLocation,
                        s_autoHarvestState.TargetLocation, &BaseDirection);
                }
            }
            else if (!MakeHorizontalAutoHarvestDirection(DinoLocation,
                MovementGoal, &BaseDirection)) {
                MakeHorizontalAutoHarvestDirection(DinoLocation,
                    s_autoHarvestState.TargetLocation, &BaseDirection);
            }

            if (MonitorGroundRecovery) {
                bool BreakableDirectObstacle = false;
                if (g_Config::bAutoHarvestBreakObstacles &&
                    IsFiniteAutoHarvestVector(BaseDirection)) {
                    SDK::FHitResult BlockingHit{};
                    BreakableDirectObstacle =
                        AutoHarvestDirectionIsBlocked(World, Dino, ProbeFrame,
                            BaseDirection,
                            s_autoHarvestState.TargetLocation,
                            TraceIgnoreActors,
                            &BlockingHit) &&
                        IsAutoHarvestBreakableObstacle(BlockingHit);
                }

                bool ClearBurstActive =
                    s_autoHarvestState.ObstacleClearUntil != Clock::time_point{} &&
                    Now < s_autoHarvestState.ObstacleClearUntil;
                if (ClearBurstActive && !BreakableDirectObstacle) {
                    // The trace opened up, so the preceding attacks most likely
                    // destroyed the foliage instance. Resume and replan now.
                    ReleaseAutoHarvestAttack();
                    s_autoHarvestState.ObstacleClearUntil = {};
                    s_autoHarvestState.ObstacleClearAttacksRemaining = 0;
                    s_autoHarvestState.ObstacleClearBursts = 0;
                    s_autoHarvestState.StationarySince = {};
                    s_autoHarvestState.RecoveryAttempts = 0;
                    s_autoHarvestState.NextPathRefresh = Clock::time_point::min();
                    ClearBurstActive = false;
                }
                else if (s_autoHarvestState.ObstacleClearUntil != Clock::time_point{} &&
                    !ClearBurstActive) {
                    ReleaseAutoHarvestAttack();
                    s_autoHarvestState.ObstacleClearUntil = {};
                    s_autoHarvestState.ObstacleClearAttacksRemaining = 0;
                    s_autoHarvestState.ObstacleClearCooldownUntil =
                        Now + std::chrono::milliseconds(1200);
                }

                const bool RecoveryNeedsClearing = ForceDetour ||
                    s_autoHarvestState.UnsafeSince != Clock::time_point{};
                if (!ClearBurstActive && BreakableDirectObstacle &&
                    RecoveryNeedsClearing &&
                    s_autoHarvestState.ObstacleClearBursts < 2 &&
                    Now >= s_autoHarvestState.ObstacleClearCooldownUntil) {
                    s_autoHarvestState.ObstacleClearUntil =
                        Now + std::chrono::milliseconds(3200);
                    s_autoHarvestState.NextObstacleAttack = Now;
                    s_autoHarvestState.ObstacleClearAttacksRemaining = 4;
                    ++s_autoHarvestState.ObstacleClearBursts;
                    ClearBurstActive = true;
                }

                if (ClearBurstActive) {
                    ClearingObstacle = true;
                    MovementDirection = BaseDirection;
                    HasMovementDirection = true;
                    s_autoHarvestState.UnsafeSince = {};
                }
                else if (UseSafeNavigation) {
                    const AutoHarvestSafeDirectionResult SafeDirection =
                        FindAutoHarvestSafeDirection(World, Dino, ProbeFrame, BaseDirection,
                            s_autoHarvestState.TargetLocation,
                            g_Config::AutoHarvestMaxSafeDropMeters, Now,
                            TraceIgnoreActors,
                            ForceDetour, (s_autoHarvestState.RecoveryAttempts % 2) == 0);
                    if (!SafeDirection.Found) {
                        ReleaseAutoHarvestAttack();
                        ReleaseAutoHarvestSprint();
                        if (s_autoHarvestState.UnsafeSince == Clock::time_point{}) {
                            s_autoHarvestState.UnsafeSince = Now;
                        }
                        s_autoHarvestState.NextPathRefresh = Harvest::BringForward(
                            s_autoHarvestState.NextPathRefresh, Now + std::chrono::milliseconds(
                                g_Config::bAutoHarvestExperimentalNavigation ? 220 : 350));

                        if (Now - s_autoHarvestState.UnsafeSince > std::chrono::seconds(3)) {
                            AddAutoHarvestSkippedTarget(s_autoHarvestState.TargetLocation,
                                Now + std::chrono::seconds(20));
                            ClearAutoHarvestTarget();
                            SetAutoHarvestStatus(SafeDirection.CliffDetected ?
                                "悬崖保护：该资源无安全路线，正在切换目标" :
                                "避障失败：该资源无可通行路线，正在切换目标");
                        }
                        else {
                            SetAutoHarvestStatus(SafeDirection.CliffDetected ?
                                "悬崖保护：前方没有安全落脚点，正在重新规划" :
                                "前方被阻挡，正在重新规划路线");
                        }
                        return;
                    }
                    MovementDirection = SafeDirection.Direction;
                    HasMovementDirection = true;
                    AvoidingObstacle = SafeDirection.Avoiding;
                    s_autoHarvestState.UnsafeSince = {};
                }
                else if (IsFiniteAutoHarvestVector(BaseDirection)) {
                    MovementDirection = BaseDirection;
                    HasMovementDirection = true;
                }
            }
            else if (IsFiniteAutoHarvestVector(BaseDirection)) {
                MovementDirection = BaseDirection;
                HasMovementDirection = true;
            }
        }

        // Swimming creatures cannot stop when movement input is released. Use
        // their current velocity to begin slowing before the attack radius,
        // counter-thrust after an overshoot, and only resume forward input once
        // the mount has settled. Facing remains on the resource while braking.
        if (IsAquaticCreature && AutoApproachEnabled) {
            SDK::FVector TargetDirection{};
            if (MakeThreeDimensionalAutoHarvestDirection(DinoLocation,
                s_autoHarvestState.TargetLocation, &TargetDirection)) {
                // Steering may counter lateral momentum, but the camera/body
                // must keep looking at the selected node rather than at the
                // temporary braking vector.
                MovementDirectionControlsFacing = false;
                const double DistanceUU = std::sqrt(DistanceSquared);
                const double AttackDistanceUU = static_cast<double>(AttackDistance) * 100.0;
                const double EdgeDistanceUU = DistanceUU - AttackDistanceUU;
                const double Speed = GetAquaticSpeed();
                SDK::FVector Velocity{};
                if (Dino->CharacterMovement && IsUsableObject(Dino->CharacterMovement)) {
                    Velocity = Dino->CharacterMovement->Velocity;
                }
                const double ClosingSpeed = Velocity.X * TargetDirection.X +
                    Velocity.Y * TargetDirection.Y + Velocity.Z * TargetDirection.Z;
                const double LookAheadSeconds = std::clamp(
                    DistanceUU / std::max(Speed, 250.0), 0.18, 0.85);
                const SDK::FVector PredictedLocation{
                    DinoLocation.X + Velocity.X * LookAheadSeconds,
                    DinoLocation.Y + Velocity.Y * LookAheadSeconds,
                    DinoLocation.Z + Velocity.Z * LookAheadSeconds
                };
                const SDK::FVector PredictedDelta{
                    s_autoHarvestState.TargetLocation.X - PredictedLocation.X,
                    s_autoHarvestState.TargetLocation.Y - PredictedLocation.Y,
                    s_autoHarvestState.TargetLocation.Z - PredictedLocation.Z
                };
                const double PredictedAlongTarget = PredictedDelta.X * TargetDirection.X +
                    PredictedDelta.Y * TargetDirection.Y + PredictedDelta.Z * TargetDirection.Z;
                const double BrakeMarginUU = std::clamp(
                    static_cast<double>(CapsuleRadius) * 1.15 + Speed * 0.60,
                    220.0, 2400.0);
                const bool PassedTarget = PredictedAlongTarget < -
                    std::max(80.0, static_cast<double>(CapsuleRadius) * 0.35);
                const bool DriftingAwayNearTarget = ClosingSpeed < -80.0 &&
                    DistanceUU <= AttackDistanceUU + BrakeMarginUU * 1.5;

                if (DistanceUU + 40.0 < s_autoHarvestState.AquaticClosestDistanceUU) {
                    s_autoHarvestState.AquaticClosestDistanceUU = DistanceUU;
                }
                else if ((PassedTarget || DriftingAwayNearTarget) &&
                    DistanceUU > s_autoHarvestState.AquaticClosestDistanceUU + 140.0) {
                    ++s_autoHarvestState.AquaticOvershootCount;
                    s_autoHarvestState.AquaticClosestDistanceUU = DistanceUU;
                    s_autoHarvestState.AquaticBrakeUntil =
                        Now + std::chrono::milliseconds(900);
                }

                if (s_autoHarvestState.AquaticOvershootCount >= 3) {
                    AddAutoHarvestSkippedTarget(s_autoHarvestState.TargetLocation,
                        Now + std::chrono::seconds(15));
                    s_autoHarvestState.AquaticSettleUntil =
                        Now + std::chrono::milliseconds(2800);
                    ReleaseAutoHarvestAttack();
                    ClearAutoHarvestTarget();
                    SetAutoHarvestStatus("水下连续越过同一资源，已刹停并暂时切换目标");
                    return;
                }

                const bool MustBrake = Speed > 55.0 &&
                    (EdgeDistanceUU <= BrakeMarginUU || PassedTarget ||
                        DriftingAwayNearTarget || Now < s_autoHarvestState.AquaticBrakeUntil);
                if (MustBrake) {
                    MovementDirection = SDK::FVector{
                        -Velocity.X / Speed, -Velocity.Y / Speed, -Velocity.Z / Speed
                    };
                    HasMovementDirection = IsFiniteAutoHarvestVector(MovementDirection);
                    MovementInputScale = static_cast<float>(std::clamp(
                        Speed / 650.0, 0.28, 1.0));
                    AquaticBraking = HasMovementDirection;
                    MovementDirectionControlsFacing = false;
                }
                else if (EdgeDistanceUU > 0.0) {
                    MovementDirection = TargetDirection;
                    HasMovementDirection = true;
                    const double SlowZoneUU = std::clamp(
                        Speed * 1.25 + static_cast<double>(CapsuleRadius) * 2.0,
                        650.0, 3600.0);
                    MovementInputScale = static_cast<float>(std::clamp(
                        EdgeDistanceUU / SlowZoneUU, 0.18, 1.0));
                    s_autoHarvestState.AquaticLastApproachDirection = TargetDirection;
                }
                else {
                    HasMovementDirection = false;
                }
            }
        }

        UpdateAutoHarvestSprint(Dino,
            g_Config::bAutoHarvestSprint && AutoApproachEnabled &&
                IsGroundCreature && NeedsApproach &&
                HasMovementDirection && !ClearingObstacle,
            Now);

        // Keep mount steering and camera aim separate. While the beam path is
        // occluded, face the route instead of forcing the camera through the
        // wall; restore the resource lock as soon as direct sight is available.
        SDK::FVector MountFacingTarget = s_autoHarvestState.TargetLocation;
        if (HasMovementDirection && MovementDirectionControlsFacing) {
            MountFacingTarget.X = DinoLocation.X + MovementDirection.X * 1000.0;
            MountFacingTarget.Y = DinoLocation.Y + MovementDirection.Y * 1000.0;
            if (UseThreeDimensionalMovement) {
                MountFacingTarget.Z = DinoLocation.Z + MovementDirection.Z * 1000.0;
            }
        }
        if (!UseThreeDimensionalMovement) {
            MountFacingTarget.Z = DinoLocation.Z;
        }
        SDK::FRotator MountFacingRotation =
            SDK::UKismetMathLibrary::FindLookAtRotation(
                DinoLocation, MountFacingTarget);
        if (!UseThreeDimensionalMovement) MountFacingRotation.Pitch = 0.0;
        MountFacingRotation.Roll = 0.0;

        SDK::FRotator TargetRotation = MountFacingRotation;
        if (TekStriderMode && TekStriderTargetVisible) {
            SDK::FVector AimOrigin = DinoLocation;
            SDK::FVector CameraLocation{};
            SDK::FRotator CameraRotation{};
            LocalPC->GetPlayerViewPoint(&CameraLocation, &CameraRotation);
            if (IsFiniteAutoHarvestVector(CameraLocation)) {
                AimOrigin = CameraLocation;
            }
            TargetRotation = SDK::UKismetMathLibrary::FindLookAtRotation(
                AimOrigin, s_autoHarvestState.TargetLocation);
        }
        if (IsAquaticCreature) {
            const double AimDeltaX = s_autoHarvestState.TargetLocation.X - DinoLocation.X;
            const double AimDeltaY = s_autoHarvestState.TargetLocation.Y - DinoLocation.Y;
            const double AimHorizontalUU = std::sqrt(
                AimDeltaX * AimDeltaX + AimDeltaY * AimDeltaY);

            // Directly above/below a node, atan2 yaw becomes unstable: tiny
            // position changes can flip it by 180 degrees and create a circle.
            // Keep the current yaw while pitching vertically, and cap pitch at
            // a value water-dino movement can actually follow.
            const double StableYawRadiusUU = std::clamp(
                static_cast<double>(CapsuleRadius) * 0.9, 140.0, 450.0);
            if (AimHorizontalUU < StableYawRadiusUU) {
                TargetRotation.Yaw = LocalPC->GetControlRotation().Yaw;
            }
            TargetRotation.Pitch = std::clamp(TargetRotation.Pitch, -75.0, 75.0);
        }
        TargetRotation.Roll = 0.0;
        s_autoHarvestState.DesiredMountFacingRotation = MountFacingRotation;
        s_autoHarvestState.HasDesiredMountFacingRotation = true;
        if (TekStriderMode && TekStriderTargetVisible) {
            PublishAutoHarvestViewLock(Dino,
                s_autoHarvestState.TargetLocation);
        }
        else {
            ClearAutoHarvestViewLock();
        }
        LocalPC->SetControlRotation(TargetRotation);
        if (IsAquaticCreature) {
            Dino->SetLastMovementDesiredRotation(TargetRotation);
        }

        // PhysicsRotation may not run while a mounted creature is completely
        // stationary. Apply the same root-component turn immediately so the
        // first harvest attack is already facing the selected node and does
        // not require a movement-key tap to wake the movement component.
        if (Dino->CharacterMovement && Dino->CharacterMovement->UpdatedComponent &&
            IsUsableObject(Dino->CharacterMovement) &&
            IsUsableObject(Dino->CharacterMovement->UpdatedComponent)) {
            const SDK::FVector SavedVelocity = Dino->CharacterMovement->Velocity;
            const SDK::FRotator DinoRotation = MountFacingRotation;
            Dino->CharacterMovement->UpdatedComponent->K2_SetWorldRotation(
                DinoRotation, false, nullptr, false);
            Dino->CharacterMovement->UpdatedComponent->RelativeRotation = DinoRotation;
            Dino->CharacterMovement->Velocity = SavedVelocity;
        }
        const SDK::FRotator AppliedDinoRotation = Dino->K2_GetActorRotation();

        bool AquaticMovementApplied = false;
        if (IsAquaticCreature && AutoApproachEnabled &&
            HasMovementDirection) {
            Dino->AddMovementInput(MovementDirection, MovementInputScale, true);
            AquaticMovementApplied = true;
        }

        if (NeedsApproach) {
            if (ClearingObstacle) {
                ReleaseAutoHarvestSprint();
                if (s_autoHarvestState.ObstacleClearAttacksRemaining > 0 &&
                    !s_autoHarvestState.AttackHeld &&
                    Now >= s_autoHarvestState.NextObstacleAttack) {
                    if (SendAutoHarvestMouseButton(true)) {
                        s_autoHarvestState.AttackHeld = true;
                        s_autoHarvestState.ReleaseAttack =
                            Now + std::chrono::milliseconds(85);
                        --s_autoHarvestState.ObstacleClearAttacksRemaining;
                    }
                    s_autoHarvestState.NextObstacleAttack =
                        Now + std::chrono::milliseconds(520);
                }

                if (s_autoHarvestState.ObstacleClearAttacksRemaining <= 0 &&
                    !s_autoHarvestState.AttackHeld) {
                    s_autoHarvestState.ObstacleClearUntil = {};
                    s_autoHarvestState.ObstacleClearCooldownUntil =
                        Now + std::chrono::milliseconds(1200);
                    s_autoHarvestState.NextPathRefresh = Clock::time_point::min();
                    s_autoHarvestState.AvoidUntil = {};
                }

                std::ostringstream Status;
                Status << "前方疑似石头或树木，正在攻击清障（本轮剩余 "
                    << std::max(0, s_autoHarvestState.ObstacleClearAttacksRemaining)
                    << " 次）";
                SetAutoHarvestStatus(Status.str());
                return;
            }
            ReleaseAutoHarvestAttack();
            std::ostringstream Status;
            if (AutoApproachEnabled) {
                if (HasMovementDirection && !AquaticMovementApplied) {
                    Dino->AddMovementInput(MovementDirection, MovementInputScale, true);
                }
                if (TekStriderTargetOccluded) {
                    Status << "光束路径被墙体或地形阻挡，正在寻找可见采集站位 ";
                }
                else if (AquaticBraking) {
                    Status << "正在水下反推减速并保持当前目标 ";
                }
                else if (AvoidingObstacle) {
                    Status << "正在绕过障碍并接近 ";
                }
                else if (IsAquaticCreature) {
                    Status << "正在水下三维接近 ";
                }
                else if (UsingNavigationPath) {
                    if (g_Config::bAutoHarvestExperimentalNavigation) {
                        Status << (s_autoHarvestState.NavigationPathPartial ?
                            "实验寻路：沿部分路径接近 " :
                            "实验寻路：沿优化路径接近 ");
                    }
                    else {
                        Status << (s_autoHarvestState.NavigationPathPartial ?
                            "正在沿部分导航路径接近 " : "正在沿导航路径接近 ");
                    }
                }
                else if (g_Config::bAutoHarvestSafeNavigation) {
                    Status << "正在使用安全探测接近 ";
                }
                else {
                    Status << "正在接近 ";
                }
                Status << s_autoHarvestState.TargetType << " ("
                    << std::fixed << std::setprecision(1) << DistanceMeters << "m";
                if (IsAquaticCreature) {
                    Status << "，目标/实际俯仰 " << std::setprecision(0)
                        << TargetRotation.Pitch << "/" << AppliedDinoRotation.Pitch << "°";
                }
                Status << ")";
            }
            else {
                Status << "已锁定 " << s_autoHarvestState.TargetType << "，距离 "
                    << std::fixed << std::setprecision(1) << DistanceMeters << "m，等待进入攻击范围";
            }
            SetAutoHarvestStatus(Status.str());
            return;
        }

        if (TekStriderMode) {
            ReleaseAutoHarvestSprint();
            ReleaseAutoHarvestAttack();
            if (!s_autoHarvestState.TekStriderBeamStartRequested &&
                Now >= s_autoHarvestState.NextTekStriderBeamToggle) {
                int32_t DinoSerialNumber = -1;
                if (TryGetLiveObjectSerial(Dino, Dino->Index,
                    &DinoSerialNumber) && SendAutoHarvestTekStriderRigToggle()) {
                    s_autoHarvestState.TekStriderBeamStartRequested = true;
                    s_autoHarvestState.TekStriderBeamDino = Dino;
                    s_autoHarvestState.TekStriderBeamDinoObjectIndex = Dino->Index;
                    s_autoHarvestState.TekStriderBeamDinoSerialNumber =
                        DinoSerialNumber;
                    s_autoHarvestState.NextTekStriderBeamToggle =
                        Now + std::chrono::milliseconds(750);
                }
            }

            SDK::FVector CameraLocation{};
            SDK::FRotator CameraRotation{};
            LocalPC->GetPlayerViewPoint(&CameraLocation, &CameraRotation);
            SDK::FVector CameraToTarget{};
            const bool HasCameraDirection =
                IsFiniteAutoHarvestVector(CameraLocation) &&
                MakeThreeDimensionalAutoHarvestDirection(CameraLocation,
                    s_autoHarvestState.TargetLocation, &CameraToTarget);
            const SDK::FVector CameraForward =
                SDK::UKismetMathLibrary::GetForwardVector(CameraRotation);
            const double AimDot = HasCameraDirection &&
                IsFiniteAutoHarvestVector(CameraForward)
                ? CameraToTarget.X * CameraForward.X +
                    CameraToTarget.Y * CameraForward.Y +
                    CameraToTarget.Z * CameraForward.Z
                : -1.0;
            // Start the idle attack timer only after the persistent beam request
            // exists. Alignment is reported separately; a beam that never lines
            // up must still expire after 14 seconds without inventory progress.
            const bool AimAligned = AimDot >= 0.995;
            if (s_autoHarvestState.TekStriderBeamStartRequested &&
                s_autoHarvestState.AttackStarted == Clock::time_point{}) {
                s_autoHarvestState.AttackStarted = Now;
            }


            std::ostringstream Status;
            Status << (s_autoHarvestState.TekStriderBeamStartRequested
                ? (AimAligned
                    ? "视角已锁定资源，跨步者采集光束持续开启 "
                    : "跨步者采集光束保持开启，正在将视角对准 ")
                : "正在将视角锁定资源并开启跨步者采集光束 ")
                << s_autoHarvestState.TargetType << " ("
                << std::fixed << std::setprecision(1) << DistanceMeters << "m)";
            SetAutoHarvestStatus(Status.str());
            return;
        }

        if (s_autoHarvestState.AttackStarted == Clock::time_point{}) {
            s_autoHarvestState.AttackStarted = Now;
        }

        SampleAutoHarvestProgress(Dino, Now);
        const int AttackIntervalMs = ConfiguredAttackIntervalMs;
        if (!s_autoHarvestState.AttackHeld && Now >= s_autoHarvestState.NextAttack) {
            if (SendAutoHarvestMouseButton(true)) {
                s_autoHarvestState.AttackHeld = true;
                s_autoHarvestState.ReleaseAttack = Now + std::chrono::milliseconds(75);
            }
            s_autoHarvestState.NextAttack = Now + std::chrono::milliseconds(AttackIntervalMs);
        }

        std::ostringstream Status;
        Status << (AquaticBraking ? "水下减速并采集 " : "正在采集 ")
            << s_autoHarvestState.TargetType << " ("
            << std::fixed << std::setprecision(1) << DistanceMeters << "m";
        if (IsAquaticCreature) {
            Status << "，目标/实际俯仰 " << std::setprecision(0)
                << TargetRotation.Pitch << "/" << AppliedDinoRotation.Pitch << "°";
        }
        Status << ")";
        SetAutoHarvestStatus(Status.str());
    }

    void SuperFlyer(SDK::UWorld* World) {
        struct SuperFlyerState {
            SDK::APrimalDinoCharacter* Dino = nullptr;
            int32_t DinoObjectIndex = -1;
            bool AllowBackwardsFlight = false;
            bool AllowStrafing = false;
        };

        static SuperFlyerState State;

        auto RestoreState = []() {
            if (State.Dino && State.DinoObjectIndex >= 0 &&
                IsUsableObject(State.Dino) && State.Dino->Index == State.DinoObjectIndex) {
                State.Dino->bFlyerDinoAllowBackwardsFlight = State.AllowBackwardsFlight;
                State.Dino->bFlyerDinoAllowStrafing = State.AllowStrafing;
            }
            State = SuperFlyerState{};
        };

        if (!g_Config::bSuperFlyer) {
            RestoreState();
            return;
        }

        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
            RestoreState();
            return;
        }

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController) {
            RestoreState();
            return;
        }

        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);
        if (!PC->Pawn || !PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::APrimalDinoCharacter* TargetDino = static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn);

        // bIsFlying alone is only a transient state and can also be used by a
        // few jump/glide/special-movement creatures. Require the dino's own
        // CanFly capability as the species-level filter, then apply the flags
        // only during actual flight. Land dinos are never cached or modified.
        if (!TargetDino->CanFly() || !TargetDino->bIsFlying) {
            RestoreState();
            return;
        }

        if (State.Dino != TargetDino || State.DinoObjectIndex != TargetDino->Index) {
            RestoreState();
            State.Dino = TargetDino;
            State.DinoObjectIndex = TargetDino->Index;
            State.AllowBackwardsFlight = TargetDino->bFlyerDinoAllowBackwardsFlight;
            State.AllowStrafing = TargetDino->bFlyerDinoAllowStrafing;
        }

        // 允许恐龙左右飞，倒飞
        TargetDino->bFlyerDinoAllowBackwardsFlight = true;
        TargetDino->bFlyerDinoAllowStrafing = true;
    }

    void ForceTurn(SDK::UMovementComponent* rcx, float DeltaTime) {
        struct ForceTurnState {
            SDK::UCharacterMovementComponent* Movement = nullptr;
            int32_t MovementObjectIndex = -1;
            bool OrientRotationToMovement = false;
            bool UseControllerDesiredRotation = false;
        };

        static ForceTurnState State;

        auto RestoreState = []() {
            if (State.Movement && State.MovementObjectIndex >= 0 &&
                IsUsableObject(State.Movement) && State.Movement->Index == State.MovementObjectIndex) {
                State.Movement->bOrientRotationToMovement = State.OrientRotationToMovement;
                State.Movement->bUseControllerDesiredRotation = State.UseControllerDesiredRotation;
            }
            State = ForceTurnState{};
        };

        if (!g_Config::bForceTurn) {
            RestoreState();
            return;
        }

        SDK::APlayerController* LocalPC = g_Util::GetLocalPC();

        if (!LocalPC || !LocalPC->Pawn) {
            RestoreState();
            return;
        }

        // 只为玩家骑乘的恐龙启用
        if (!LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            RestoreState();
            return;
        }

        SDK::APrimalDinoCharacter* TargetDino = static_cast<SDK::APrimalDinoCharacter*>(LocalPC->Pawn);

        // 由于此函数是共用的，所以必须过滤掉除player与riding外的movement。最好的方法是每帧检查并hook虚函数
        // 可以增加一个过滤，只为恐龙启用
        if (!TargetDino->CharacterMovement ||
            reinterpret_cast<uintptr_t>(TargetDino->CharacterMovement) != reinterpret_cast<uintptr_t>(rcx)) {
            return;
        }

        if (!LocalPC->PlayerCameraManager) {
            RestoreState();
            return;
        }

        // 转换为 UCharacterMovementComponent 以访问其属性
        SDK::UCharacterMovementComponent* moveComp = static_cast<SDK::UCharacterMovementComponent*>(rcx);
        if (!moveComp) return;

        if (State.Movement != moveComp || State.MovementObjectIndex != moveComp->Index) {
            RestoreState();
            State.Movement = moveComp;
            State.MovementObjectIndex = moveComp->Index;
            State.OrientRotationToMovement = moveComp->bOrientRotationToMovement;
            State.UseControllerDesiredRotation = moveComp->bUseControllerDesiredRotation;
        }

        // 禁用所有自动旋转机制
        moveComp->bOrientRotationToMovement = false;
        moveComp->bUseControllerDesiredRotation = false;

        // Auto harvesting keeps camera aim and movement-facing independent.
        // In particular, a Strider can follow a safe detour without dragging
        // the excavation view away from the selected resource.
        SDK::FRotator rot = g_Config::bAutoHarvestResources &&
            s_autoHarvestState.HasDesiredMountFacingRotation
            ? s_autoHarvestState.DesiredMountFacingRotation
            : (g_Config::bAutoHarvestResources
                ? LocalPC->GetControlRotation()
                : LocalPC->PlayerCameraManager->GetCameraRotation());
        if (g_Config::bAutoHarvestResources &&
            g_Config::bAutoHarvestTekStrider &&
            !s_autoHarvestState.HasDesiredMountFacingRotation) {
            rot.Pitch = 0.0;
            rot.Roll = 0.0;
        }

        if (!moveComp->UpdatedComponent) return;

        // 保存当前速度
        SDK::FVector oldVelocity = moveComp->Velocity;

        // 执行旋转（强制更新世界旋转和相对旋转）
        moveComp->UpdatedComponent->K2_SetWorldRotation(rot, false, nullptr, false);
        moveComp->UpdatedComponent->RelativeRotation = rot;

        // 恢复速度（消除旋转引起的任何速度变化）
        moveComp->Velocity = oldVelocity;
    }

    void DamageLog(SDK::AActor* _this, float DamageAmount, SDK::FDamageEvent* DamageEvent, SDK::AController* Instigator, SDK::AActor* DamageCauser) {
        SDK::UWorld* World = SDK::UWorld::GetWorld();
        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController || !LP->PlayerController->Pawn) return;

        SDK::APawn* LPawn = LP->PlayerController->Pawn;
        if (!LPawn || DamageCauser != LPawn || _this == LPawn) return;

        std::string targetName = "";
        std::string causerName = "";
        float rhp = 0.f;

        // 1. 处理受击者 (Target)
        if (_this->IsA(SDK::AShooterCharacter::StaticClass())) {
            rhp = static_cast<SDK::AShooterCharacter*>(_this)->GetHealth() - DamageAmount;
            targetName = static_cast<SDK::AShooterCharacter*>(_this)->PlayerName.ToString();
        }
        else if (_this->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            rhp = static_cast<SDK::APrimalDinoCharacter*>(_this)->GetHealth() - DamageAmount;
            targetName = static_cast<SDK::APrimalDinoCharacter*>(_this)->DescriptiveName.ToString();
        }
        else if (_this->IsA(SDK::APrimalStructure::StaticClass())) {
            rhp = static_cast<SDK::APrimalStructure*>(_this)->Health - DamageAmount;
            targetName = static_cast<SDK::APrimalStructure*>(_this)->GetDescriptiveName().ToString();
        }
        else {
            return;
            // 这种情况一般是树之类的，不确定还有没有其他类型的实体
            // targetName = _this->Class->GetName();
        }

        if (rhp < 0) rhp = 0;

        // 2. 处理攻击者 (Causer)
        if (DamageCauser->IsA(SDK::AShooterCharacter::StaticClass())) {
            causerName = static_cast<SDK::AShooterCharacter*>(DamageCauser)->PlayerName.ToString();
        }
        else if (DamageCauser->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            causerName = static_cast<SDK::APrimalDinoCharacter*>(DamageCauser)->DescriptiveName.ToString();
        }
        else if (DamageCauser->IsA(SDK::APrimalStructure::StaticClass())) {
            causerName = static_cast<SDK::APrimalStructure*>(DamageCauser)->GetDescriptiveName().ToString();
        }
        else {
            causerName = DamageCauser->Class->GetName();
        }

        // 3. 打印日志
        std::string logMsg = std::format("{} -> {} -> {:.0f} damage ({:.0f} remaining)",
            causerName,
            targetName,
            DamageAmount,
            rhp
        );

        g_LogManager::AddLog(
            g_Config::LogDamageColor[0] * 255,
            g_Config::LogDamageColor[1] * 255,
            g_Config::LogDamageColor[2] * 255,
            g_Config::LogDamageColor[3] * 255,
            logMsg
        );
    }

    namespace {
        // 副手盾牌与五个身体部位共用自动换甲入口，按部位开关筛选。
        bool ArmorSwapSlotEnabled(int TypeIndex) {
            switch (TypeIndex) {
                case 0: return g_Config::bArmorSwapHead;
                case 1: return g_Config::bArmorSwapShirt;
                case 2: return g_Config::bArmorSwapPants;
                case 3: return g_Config::bArmorSwapBoots;
                case 4: return g_Config::bArmorSwapGloves;
                case 8: return g_Config::bArmorSwapShield;
                default: return false;
            }
        }
        const char* ArmorSlotName(int TypeIndex) {
            switch (TypeIndex) {
                case 0: return "头盔";
                case 1: return "胸甲";
                case 2: return "护腿";
                case 3: return "靴子";
                case 4: return "手套";
                case 8: return "盾牌";
                default: return "未知部位";
            }
        }
    }

    static std::mutex g_ArmorSwapStatusMutex;
    static std::string g_ArmorSwapStatus = "自动换甲未启用";

    std::string GetArmorSwapStatus() {
        std::scoped_lock Lock(g_ArmorSwapStatusMutex);
        return LanguageManager::Get().TranslateCopy(g_ArmorSwapStatus);
    }

    void AutoSwapBrokenEquipment()
    {
        static int globalTick = 0;
        globalTick++;

        if (globalTick % 3 != 0) return;

        if (!g_Config::bAutomatic) {
            std::scoped_lock Lock(g_ArmorSwapStatusMutex);
            g_ArmorSwapStatus = "自动换甲未启用";
            return;
        }

        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(
            g_Util::GetLocalPC());
        if (!IsUsableObject(PC)) return;

        SDK::APrimalCharacter* Character = GetPlayerInventoryOwner(PC);
        if (!IsUsableObject(Character) ||
            !Character->IsA(SDK::AShooterCharacter::StaticClass()) ||
            Character->IsA(SDK::APrimalDinoCharacter::StaticClass()) ||
            !IsUsableObject(Character->MyInventoryComponent)) return;

        auto Inv = Character->MyInventoryComponent;
        auto& EquippedItems = Inv->EquippedItems;
        auto& InventoryItems = Inv->InventoryItems;

        const int32_t EquippedItemCount = EquippedItems.Num();
        const int32_t InventoryItemCount = InventoryItems.Num();
        if (EquippedItemCount < 0 || EquippedItemCount > 100000 ||
            InventoryItemCount <= 0 || InventoryItemCount > 100000) return;

        static int lastSwapTick[25] = { 0 };
        struct ArmorSwapSlotRuntime {
            int LastRequestTick = -100000;
            SDK::FItemNetID LastRequestedID{};
            int FailedAttempts = 0;
            SDK::FItemNetID BlacklistedID{};
            ULONGLONG BlacklistUntil = 0;
            bool PendingFill = false;   // 摘除皮肤后允许补穿空槽
            int PendingFillTick = -100000;
        };
        static ArmorSwapSlotRuntime SlotState[25];
        static ULONGLONG NextSyncRequestAt = 0;
        const float ALERT_DURABILITY = g_Config::ArmorRange;

        // 获取当前穿着的装备。皮肤（bIsItemSkin）与护甲可以同槽位同时
        // 存在且皮肤没有耐久：若用皮肤覆盖护甲记录，底下的护甲碎了也
        // 不会被替换（手臂手套最容易长期挂皮肤，导致“无法换手臂”）。
        // 因此真实护甲与皮肤分开记录：有护甲看护甲，只剩皮肤按空槽处理。
        SDK::UPrimalItem* WornArmor[20] = { nullptr };
        SDK::UPrimalItem* WornSkin[20] = { nullptr };
        for (int i = 0; i < EquippedItemCount; i++)
        {
            auto item = EquippedItems[i];
            if (!IsUsableObject(item)) continue;

            int typeIndex = (int)item->MyEquipmentType;
            if (typeIndex < 0 || typeIndex >= 20) continue;
            if (item->bIsItemSkin) {
                if (!WornSkin[typeIndex]) WornSkin[typeIndex] = item;
            }
            else {
                WornArmor[typeIndex] = item;
            }
        }

        int targetSlots[] = { 0, 1, 2, 3, 4, 8 };
        bool bSlotNeedsReplace[25] = { false }; // 记录需换甲
        bool bAnyNeedsReplace = false;          // 是否需要换

        for (int i = 0; i < 6; i++)
        {
            int typeIndex = targetSlots[i];
            if (!ArmorSwapSlotEnabled(typeIndex)) continue;

            auto& State = SlotState[typeIndex];
            auto CurrentEquippedItem = WornArmor[typeIndex];
            // 只剩皮肤说明底层护甲已碎并被移除，需要补穿
            const bool SkinOnly = CurrentEquippedItem == nullptr && WornSkin[typeIndex] != nullptr;

            // 请求后槽位已出现护甲 → 本次替换生效，重置失败计数与待补穿标记
            if (CurrentEquippedItem != nullptr && (State.FailedAttempts || State.PendingFill)) {
                State.FailedAttempts = 0;
                State.PendingFill = false;
                State.LastRequestedID = {};
            }

            // 如果在冷却中，直接跳过，本帧不处理它
            if (globalTick - lastSwapTick[typeIndex] < 60) continue;

            if (CurrentEquippedItem == nullptr)
            {
                // 空槽位：默认不自动穿戴，除非允许穿戴空槽、该槽只剩皮肤、
                // 或刚摘除皮肤等待补穿。
                if (!g_Config::bArmorSwapOnlyEquipped || SkinOnly || State.PendingFill)
                {
                    bSlotNeedsReplace[typeIndex] = true;
                    bAnyNeedsReplace = true;
                }
                else if (State.PendingFill && globalTick - State.PendingFillTick > 300) {
                    // 摘皮后长时间未穿上，放弃本次补穿
                    State.PendingFill = false;
                }
            }
            else
            {
                // 有装备，判断是否快碎了
                if (CurrentEquippedItem->bUseItemDurability &&
                    CurrentEquippedItem->ItemDurability <= ALERT_DURABILITY)
                {
                    bSlotNeedsReplace[typeIndex] = true;
                    bAnyNeedsReplace = true;
                }
            }
        }

        //如果没有需要换的 跳过
        if (!bAnyNeedsReplace) return;

        // 只剩皮肤的槽位先摘皮肤：服务器拒绝在有皮肤的槽位上穿护甲时
        // 这一步能解除阻挡；下几帧皮肤消失后再补穿。
        for (int i = 0; i < 6; i++)
        {
            int typeIndex = targetSlots[i];
            if (!bSlotNeedsReplace[typeIndex] || WornArmor[typeIndex]) continue;
            auto* Skin = WornSkin[typeIndex];
            if (!Skin) continue;
            auto& State = SlotState[typeIndex];
            if (globalTick - lastSwapTick[typeIndex] < 60) continue;
            PC->ServerRemovePawnItem(Skin->ItemID, false);
            lastSwapTick[typeIndex] = globalTick;
            State.PendingFill = true;
            State.PendingFillTick = globalTick;
            {
                std::scoped_lock Lock(g_ArmorSwapStatusMutex);
                g_ArmorSwapStatus = std::format("已请求摘除{}上的皮肤，随后补穿护甲", ArmorSlotName(typeIndex));
            }
            return; // 每帧只发一个请求
        }

        float bestDurabilityForSlot[25];
        SDK::FItemNetID bestItemNetIdForSlot[25]{};
        bool hasBestForSlot[25] = { false }; // 记录最高耐久装备
        for (int i = 0; i < 25; i++) bestDurabilityForSlot[i] = -1.0f;

        const ULONGLONG NowMs = GetTickCount64();
        // 只遍历一次大背包
        for (int j = 0; j < InventoryItemCount; j++)
        {
            auto CandItem = InventoryItems[j];
            if (!IsUsableObject(CandItem)) continue;

            int typeIndex = (int)CandItem->MyEquipmentType;

            if (typeIndex >= 0 && typeIndex < 25 && bSlotNeedsReplace[typeIndex])
            {
                if (CandItem->bIsItemSkin || !CandItem->bUseItemDurability) continue;
                if (CandItem->bIsBlueprint || CandItem->bIsEngram) continue;
                if (CandItem->ItemQuantity <= 0 || CandItem->bIsRepairing) continue;
                // 跳过当前正穿着的同一件（本地耐久视图滞后时避免重新穿回碎甲）
                if (WornArmor[typeIndex] &&
                    CandItem->ItemID.ItemID1 == WornArmor[typeIndex]->ItemID.ItemID1 &&
                    CandItem->ItemID.ItemID2 == WornArmor[typeIndex]->ItemID.ItemID2) continue;
                // 服务器连续拒绝过的备件先冷却，改试其它备件
                auto& State = SlotState[typeIndex];
                if (NowMs < State.BlacklistUntil &&
                    CandItem->ItemID.ItemID1 == State.BlacklistedID.ItemID1 &&
                    CandItem->ItemID.ItemID2 == State.BlacklistedID.ItemID2) continue;

                float candDur = CandItem->ItemDurability;
                if (candDur > ALERT_DURABILITY && candDur > bestDurabilityForSlot[typeIndex])
                {
                    bestDurabilityForSlot[typeIndex] = candDur;
                    bestItemNetIdForSlot[typeIndex] = CandItem->ItemID;
                    hasBestForSlot[typeIndex] = true;
                }
            }
        }

        // 每帧只发一个装备请求：同一帧内连续发送多个 ServerEquipPawnItem
        // 会相互挤占背包同步，排在后面的槽位（常见于手套、盾牌）被丢弃，
        // 表现为“其他部位能换、手臂换不了”。改为逐槽排队，下帧继续。
        for (int i = 0; i < 6; i++)
        {
            int typeIndex = targetSlots[i];

            if (bSlotNeedsReplace[typeIndex])
            {
                auto& State = SlotState[typeIndex];
                if (hasBestForSlot[typeIndex])
                {
                    // 同一备件连续请求多次仍未生效：服务器多半拒绝了它
                    // （本地耐久/状态视图可能滞后）。拉黑该备件、强制同步
                    // 一次背包数据，改试其它备件。
                    const bool SameAsLast = State.LastRequestedID.ItemID1 == bestItemNetIdForSlot[typeIndex].ItemID1 &&
                        State.LastRequestedID.ItemID2 == bestItemNetIdForSlot[typeIndex].ItemID2;
                    if (SameAsLast && State.FailedAttempts >= 3) {
                        State.BlacklistedID = bestItemNetIdForSlot[typeIndex];
                        State.BlacklistUntil = NowMs + 60000;
                        State.FailedAttempts = 0;
                        State.LastRequestedID = {};
                        if (NowMs >= NextSyncRequestAt) {
                            NextSyncRequestAt = NowMs + 30000;
                            Inv->ServerRequestItems(PC, true, false);
                        }
                        std::scoped_lock Lock(g_ArmorSwapStatusMutex);
                        g_ArmorSwapStatus = std::format("服务器未接受{}替换，已跳过该备件并同步背包", ArmorSlotName(typeIndex));
                        lastSwapTick[typeIndex] = globalTick;
                        break;
                    }
                    PC->ServerEquipPawnItem(bestItemNetIdForSlot[typeIndex]);
                    State.FailedAttempts = SameAsLast ? State.FailedAttempts + 1 : 1;
                    State.LastRequestedID = bestItemNetIdForSlot[typeIndex];
                    State.LastRequestTick = globalTick;
                    lastSwapTick[typeIndex] = globalTick;
                    {
                        std::scoped_lock Lock(g_ArmorSwapStatusMutex);
                        g_ArmorSwapStatus = std::format("已请求替换{}", ArmorSlotName(typeIndex));
                    }
                    break; // 剩余槽位下一帧继续
                }
                else
                {
                    lastSwapTick[typeIndex] = globalTick; // 无可用备件，进入冷却
                    std::scoped_lock Lock(g_ArmorSwapStatusMutex);
                    g_ArmorSwapStatus = std::format("{}需要替换，但背包没有可用备件", ArmorSlotName(typeIndex));
                }
            }
        }
    }

#include "GameplayExtensions.inl"
#include "GachaOpen.inl"
#include "InventoryDeposit.inl"
#include "FeatureExpansion.inl"
#include "SpiderZiplineExperiment.inl"
#include "TekEquipment.inl"
#include "PlacementExperiment.inl"
#include "WeaponWaterAccess.inl"
#include "DinoHarvestRange.inl"
#include "UnderwaterWeapons.inl"
#include "BackpackTools.inl"
#include "SessionSnapshot.inl"
#include "GcmLocalFlight.inl"
#include "NoteExperiment.inl"
#include "WorldUtilities.inl"
#include "InstantReload.inl"

}
