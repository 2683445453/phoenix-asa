#pragma once
#include "../Config/Configs.h"
#include "../Util/Util.h"
#include "../Log/LogManager.h"
#include "../ESP/DrawESP.h"
#include "Hack.h"
#include "../../external/CppSDK/SDK/ShooterGame_parameters.hpp"
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../../external/ezVMT/include/ezVMT.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace g_Hack {
    static int32_t g_CurrentNoteIndex = 0;
    static const int32_t g_MaxNoteIndex = 1000;

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

    void UnlockExplorerNotes(SDK::UWorld* World) {
        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController || !LP->PlayerController->Pawn) return;

        if (!LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) return;
        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);

        if (!PC)  return;

        if (g_CurrentNoteIndex <= g_MaxNoteIndex) {
            PC->UnlockExplorerNote(g_CurrentNoteIndex, true, true);
            g_CurrentNoteIndex++;
        }
        else {
            g_CurrentNoteIndex = 0;
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
            if (!Item || Item->ItemQuantity <= 0) return false;

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

            if (PC->Pawn && PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                SDK::APrimalDinoCharacter* Dino = static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn);
                if (SDK::AShooterCharacter* Rider = Dino->Rider.Get()) {
                    return static_cast<SDK::APrimalCharacter*>(Rider);
                }
            }

            if (PC->Pawn && PC->Pawn->IsA(SDK::APrimalCharacter::StaticClass())) {
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
        std::string g_FishingRodMaintenanceStatus = "鱼竿维护未启用";
        std::string g_TekChestJetStatus = "泰克胸甲喷射无冷却未启用";
        std::string g_TekGloveDashStatus = "泰克手套冲刺无冷却未启用";

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
            for (int32_t Index = 0; Index < Inventory->InventoryItems.Num(); ++Index) {
                SDK::UPrimalItem* Item = Inventory->InventoryItems[Index];
                if (!IsSafeAutoConsumable(Item, TargetValue)) continue;

                const float Score = GetConsumableRestoreAmount(Item, TargetValue);
                if (Score > BestScore) {
                    BestScore = Score;
                    BestItem = Item;
                }
            }
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
            for (int32_t Index = 0; Index < Inventory->InventoryItems.Num(); ++Index) {
                SDK::UPrimalItem* Item = Inventory->InventoryItems[Index];
                if (IsRequestedBrew(Item, TargetValue)) return Item;
            }
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

        SDK::UObject* FindActiveFishingPrompt(const std::chrono::steady_clock::time_point Now,
            SDK::FKey* OutKey, int32_t* OutKeyIndex) {
            using Clock = std::chrono::steady_clock;
            static int32_t CachedObjectIndex = -1;
            static Clock::time_point NextFullScan = Clock::time_point::min();
            if (!OutKey || !OutKeyIndex) return nullptr;
            *OutKey = SDK::FKey{};
            *OutKeyIndex = -1;

            auto GetActivePromptAtIndex = [&](int32_t ObjectIndex) -> SDK::UObject* {
                if (ObjectIndex < 0 || ObjectIndex >= SDK::UObject::GObjects->Num()) return nullptr;

                SDK::UObject* Object = SDK::UObject::GObjects->GetByIndex(ObjectIndex);
                if (!IsUsableObject(Object) || Object->Index != ObjectIndex) return nullptr;

                SDK::UClass* WidgetClass = SDK::UWidget::StaticClass();
                if (!WidgetClass || !Object->IsA(WidgetClass)) return nullptr;
                if (!IsFishingRelatedObject(Object)) return nullptr;

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

            if (SDK::UObject* CachedPrompt = GetActivePromptAtIndex(CachedObjectIndex)) {
                return CachedPrompt;
            }

            CachedObjectIndex = -1;
            if (Now < NextFullScan) return nullptr;
            NextFullScan = Now + std::chrono::milliseconds(100);

            for (int32_t i = 0; i < SDK::UObject::GObjects->Num(); ++i) {
                if (SDK::UObject* Prompt = GetActivePromptAtIndex(i)) {
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

            // Z is the default OutBody hotkey. Mark only the synthetic fishing
            // press so our WndProc hook forwards it to ASA instead of consuming it.
            const bool BypassOutBody = KeyName[0] == 'z';
            if (BypassOutBody && Pressed) {
                g_MDX12::g_InputState::g_bypassOutBodyHotkeyForFishing.store(true);
            }
            const bool Sent = SendInput(1, &Input, sizeof(Input)) == 1;
            if (BypassOutBody && (!Pressed || !Sent)) {
                g_MDX12::g_InputState::g_bypassOutBodyHotkeyForFishing.store(false);
            }
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
                !EqualsNameIgnoreCase(OuterProperty->ClassPrivate->Name.ToString(), "StructProperty")) {
                return nullptr;
            }

            SDK::FStructProperty* StructProperty = static_cast<SDK::FStructProperty*>(OuterProperty);
            if (!StructProperty->Struct) return nullptr;
            SDK::FProperty* InnerProperty = FindReflectedProperty(StructProperty->Struct, FloatPropertyName);
            if (!InnerProperty || !InnerProperty->ClassPrivate || InnerProperty->Offset < 0 ||
                InnerProperty->ElementSize < sizeof(float) ||
                InnerProperty->Offset + static_cast<int32_t>(sizeof(float)) > OuterProperty->ElementSize ||
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

            if (PC->Pawn && PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                return static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn)->Rider.Get();
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

        struct AutoCraftRuntimeState {
            SDK::UWorld* World = nullptr;
            std::vector<g_Hack::AutoCraftStationView> Catalog;
            int SelectedInventoryObjectIndex = -1;
            int SelectedItemObjectIndex = -1;
            SDK::UPrimalInventoryComponent* DiscoveryInventory = nullptr;
            int DiscoveryInventoryObjectIndex = -1;
            std::chrono::steady_clock::time_point DiscoveryRequestedAt =
                std::chrono::steady_clock::time_point::min();
            std::chrono::steady_clock::time_point NextDiscoveryRequest =
                std::chrono::steady_clock::time_point::min();
            std::string Status = "请选择工作台和印痕/图纸";
            std::chrono::steady_clock::time_point NextCatalogRefresh =
                std::chrono::steady_clock::time_point::min();
            std::chrono::steady_clock::time_point NextCraftRequest =
                std::chrono::steady_clock::time_point::min();
        };

        AutoCraftRuntimeState g_AutoCraftRuntime;
        std::string g_AutoFeedStatus = "等待范围内恐龙";
        std::string g_AutoImprintStatus = "等待附近幼龙提出留痕需求";
        std::string g_AutoGrindHelicoprionStatus = "未启用";

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

            bool StationStillValid = false;
            bool RecipeStillValid = false;
            for (const g_Hack::AutoCraftStationView& Station : g_AutoCraftRuntime.Catalog) {
                if (Station.InventoryObjectIndex != g_AutoCraftRuntime.SelectedInventoryObjectIndex) continue;
                StationStillValid = true;
                for (const g_Hack::AutoCraftRecipeView& Recipe : Station.Recipes) {
                    if (Recipe.ItemObjectIndex == g_AutoCraftRuntime.SelectedItemObjectIndex) {
                        RecipeStillValid = true;
                        break;
                    }
                }
                break;
            }
            if (!StationStillValid) {
                g_AutoCraftRuntime.SelectedInventoryObjectIndex = -1;
                g_AutoCraftRuntime.SelectedItemObjectIndex = -1;
            }
            else if (!RecipeStillValid) {
                g_AutoCraftRuntime.SelectedItemObjectIndex = -1;
            }
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
            std::unordered_set<std::string> LostColonySkillNames;
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
            g_TastyBaitOverrideRuntime.ControllerOriginalProcessEvent = nullptr;
            g_TastyBaitOverrideRuntime.Provider = nullptr;
            g_TastyBaitOverrideRuntime.ProviderObjectIndex = -1;
            g_TastyBaitOverrideRuntime.GetProviderSkillModifierFunction = nullptr;
            g_TastyBaitOverrideRuntime.BPGetProviderSkillModifierFunction = nullptr;
            g_TastyBaitOverrideRuntime.ProviderOriginalProcessEvent = nullptr;
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
            g_DlcSkillRuntime.LostColonySkillNames.clear();
        }

        struct TekCooldownClearResult {
            int32_t ClearedCount = 0;
            std::string ClearedNames;
            std::string ActiveNames;
        };

        bool HasAnyNormalizedToken(const std::string& Value,
            std::initializer_list<const char*> Tokens) {
            return std::any_of(Tokens.begin(), Tokens.end(), [&](const char* Token) {
                return Token && Value.find(Token) != std::string::npos;
                });
        }

        TekCooldownClearResult ClearTekSkillCooldowns(
            SDK::AShooterCharacter* Human,
            std::initializer_list<const char*> SubjectTokens,
            std::initializer_list<const char*> StrongIdentityTokens) {
            TekCooldownClearResult Result;
            if (!Human || !IsUsableObject(Human)) return Result;

            ReflectedStructArrayView Cooldowns;
            if (!GetReflectedStructArray(Human, "SkillCooldowns", 512, &Cooldowns) ||
                !Cooldowns.ElementStruct) {
                return Result;
            }

            SDK::FProperty* SkillNameProperty =
                FindReflectedProperty(Cooldowns.ElementStruct, "SkillName");
            SDK::FProperty* LastUsedTimeProperty =
                FindReflectedProperty(Cooldowns.ElementStruct, "LastUsedTime");
            SDK::FProperty* CooldownDurationProperty =
                FindReflectedProperty(Cooldowns.ElementStruct, "CooldownDuration");
            if (!SkillNameProperty || !SkillNameProperty->ClassPrivate ||
                SkillNameProperty->Offset < 0 ||
                SkillNameProperty->Offset + static_cast<int32_t>(sizeof(SDK::FName)) >
                    Cooldowns.ElementSize ||
                !EqualsNameIgnoreCase(SkillNameProperty->ClassPrivate->Name.ToString(),
                    "NameProperty") ||
                !LastUsedTimeProperty || !LastUsedTimeProperty->ClassPrivate ||
                LastUsedTimeProperty->Offset < 0 ||
                LastUsedTimeProperty->Offset + static_cast<int32_t>(sizeof(double)) >
                    Cooldowns.ElementSize ||
                !EqualsNameIgnoreCase(LastUsedTimeProperty->ClassPrivate->Name.ToString(),
                    "DoubleProperty") ||
                !CooldownDurationProperty || !CooldownDurationProperty->ClassPrivate ||
                CooldownDurationProperty->Offset < 0 ||
                CooldownDurationProperty->Offset + static_cast<int32_t>(sizeof(float)) >
                    Cooldowns.ElementSize ||
                !EqualsNameIgnoreCase(CooldownDurationProperty->ClassPrivate->Name.ToString(),
                    "FloatProperty")) {
                return Result;
            }

            const double GameTime = SDK::UGameplayStatics::GetTimeSeconds(Human);
            int32_t ActiveNameCount = 0;
            for (int32_t Index = 0; Index < Cooldowns.Num; ++Index) {
                uint8_t* Element = Cooldowns.Data +
                    static_cast<size_t>(Index) * Cooldowns.ElementSize;
                const SDK::FName SkillNameValue =
                    *reinterpret_cast<const SDK::FName*>(Element + SkillNameProperty->Offset);
                const std::string SkillName = SkillNameValue.ToString();
                const std::string NormalizedName = NormalizeGachaItemName(SkillName);

                const double LastUsedTime = *reinterpret_cast<const double*>(
                    Element + LastUsedTimeProperty->Offset);
                const float Duration = *reinterpret_cast<const float*>(
                    Element + CooldownDurationProperty->Offset);
                const double Remaining = LastUsedTime + static_cast<double>(Duration) - GameTime;
                if (std::isfinite(Remaining) && Remaining > 0.01 && ActiveNameCount < 6) {
                    if (!Result.ActiveNames.empty()) Result.ActiveNames += ", ";
                    Result.ActiveNames += SkillName;
                    ++ActiveNameCount;
                }

                const bool HasSubject = HasAnyNormalizedToken(NormalizedName, SubjectTokens);
                const bool HasStrongIdentity =
                    HasAnyNormalizedToken(NormalizedName, StrongIdentityTokens);
                const bool HasTekIdentity = NormalizedName.find("tek") != std::string::npos;
                if (!HasSubject || (!HasTekIdentity && !HasStrongIdentity)) continue;

                *reinterpret_cast<double*>(Element + LastUsedTimeProperty->Offset) = -1000000000.0;
                if (!Result.ClearedNames.empty()) Result.ClearedNames += ", ";
                Result.ClearedNames += SkillName;
                ++Result.ClearedCount;
            }
            return Result;
        }

        struct TekStriderRigRequestState {
            std::mutex Mutex;
            bool Pending = false;
            int HeadRigIndex = 0;
            int BodyRigIndex = 1;
            std::string Status = "瞄准一只跨步者或骑乘跨步者后应用；仅限单机/主机";
        };

        TekStriderRigRequestState g_TekStriderRigRequest;

        constexpr std::array<const char*, 4> TekStriderHeadRigClasses = {
            "PrimalItemArmor_TekStriderHarvester_C",
            "PrimalItemArmor_TekStriderMachinegun_C",
            "PrimalItemArmor_TekStriderRadar_C",
            "PrimalItemArmor_TekStriderSilenceCannon_C"
        };

        constexpr std::array<const char*, 4> TekStriderBodyRigClasses = {
            "PrimalItemArmor_TekStriderLargeCannon_C",
            "PrimalItemArmor_TekStriderSaddlebags_C",
            "PrimalItemArmor_TekStriderShield_C",
            "PrimalItemArmor_TekStriderOnesidedShield_C"
        };

        constexpr std::array<const wchar_t*, 4> TekStriderHeadRigPaths = {
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderHarvester.PrimalItemArmor_TekStriderHarvester_C",
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderMachinegun.PrimalItemArmor_TekStriderMachinegun_C",
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderRadar.PrimalItemArmor_TekStriderRadar_C",
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderSilenceCannon.PrimalItemArmor_TekStriderSilenceCannon_C"
        };

        constexpr std::array<const wchar_t*, 4> TekStriderBodyRigPaths = {
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderLargeCannon.PrimalItemArmor_TekStriderLargeCannon_C",
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderSaddlebags.PrimalItemArmor_TekStriderSaddlebags_C",
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderShield.PrimalItemArmor_TekStriderShield_C",
            L"/Game/Genesis2/CoreBlueprints/Items/Saddle/PrimalItemArmor_TekStriderOnesidedShield.PrimalItemArmor_TekStriderOnesidedShield_C"
        };

        void SetTekStriderRigStatus(std::string Status) {
            std::scoped_lock Lock(g_TekStriderRigRequest.Mutex);
            g_TekStriderRigRequest.Status = std::move(Status);
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

        SDK::APrimalDinoCharacter* ResolveTekStriderTarget(SDK::UWorld* World) {
            SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
            if (!PC) return nullptr;

            auto AsTekStrider = [](SDK::UObject* Object) -> SDK::APrimalDinoCharacter* {
                return IsTekStrider(Object)
                    ? static_cast<SDK::APrimalDinoCharacter*>(Object)
                    : nullptr;
            };

            if (SDK::APrimalDinoCharacter* PawnDino = AsTekStrider(PC->Pawn)) {
                return PawnDino;
            }

            SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
            if (!Human) return nullptr;
            if (SDK::APrimalDinoCharacter* Mounted = AsTekStrider(Human->MountedDino.Get())) {
                return Mounted;
            }

            SDK::FHitResult Hit{};
            Human->GetAimedActor(&Hit, SDK::ECollisionChannel::ECC_Visibility,
                30000.0f, 8.0f, true, true);

            SDK::AActor* HitActor = nullptr;
            if (SDK::UPrimitiveComponent* Component = Hit.Component.Get()) {
                HitActor = Component->GetOwner();
            }
            if (!HitActor) {
                const auto* Reference = reinterpret_cast<
                    const SDK::TWeakObjectPtr<SDK::UObject>*>(&Hit.HitObjectHandle);
                SDK::UObject* HitObject = Reference->Get();
                if (IsUsableObject(HitObject) && HitObject->IsA(SDK::AActor::StaticClass())) {
                    HitActor = static_cast<SDK::AActor*>(HitObject);
                }
            }

            for (int Depth = 0; HitActor && Depth < 4; ++Depth) {
                if (SDK::APrimalDinoCharacter* Target = AsTekStrider(HitActor)) return Target;
                HitActor = HitActor->GetOwner();
            }
            return nullptr;
        }

        SDK::UClass* FindClassInReflectedArray(SDK::UObject* Object,
            const char* PropertyName, const char* DesiredClassName, int32_t* OutIndex) {
            if (OutIndex) *OutIndex = -1;
            if (!IsUsableObject(Object) || !Object->Class) return nullptr;

            SDK::FProperty* Property = FindReflectedProperty(Object->Class, PropertyName);
            if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
                !EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "ArrayProperty")) {
                return nullptr;
            }

            SDK::FArrayProperty* ArrayProperty = static_cast<SDK::FArrayProperty*>(Property);
            if (!ArrayProperty->InnerProperty || !ArrayProperty->InnerProperty->ClassPrivate ||
                ArrayProperty->InnerProperty->ElementSize != sizeof(SDK::UClass*)) {
                return nullptr;
            }
            const std::string InnerType = ArrayProperty->InnerProperty->ClassPrivate->Name.ToString();
            if (!EqualsNameIgnoreCase(InnerType, "ClassProperty") &&
                !EqualsNameIgnoreCase(InnerType, "ObjectProperty")) {
                return nullptr;
            }

            struct RawClassArray {
                SDK::UClass** Data;
                int32_t Num;
                int32_t Max;
            };
            const RawClassArray* Classes = reinterpret_cast<const RawClassArray*>(
                reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
            if (Classes->Num < 0 || Classes->Num > 64 || Classes->Max < Classes->Num ||
                Classes->Max > 256 || (Classes->Num > 0 && !Classes->Data)) {
                return nullptr;
            }

            for (int32_t Index = 0; Index < Classes->Num; ++Index) {
                SDK::UClass* ItemClass = Classes->Data[Index];
                if (!IsUsableObject(ItemClass) ||
                    !EqualsNameIgnoreCase(ItemClass->GetName(), DesiredClassName)) {
                    continue;
                }
                if (OutIndex) *OutIndex = Index;
                return ItemClass;
            }
            return nullptr;
        }

        SDK::UClass* ResolveTekStriderRigClass(SDK::APrimalDinoCharacter* Target,
            const char* ArrayPropertyName, const char* ClassName, const wchar_t* AssetPath,
            int32_t* OutAttachmentIndex) {
            SDK::UClass* Result = FindClassInReflectedArray(
                Target, ArrayPropertyName, ClassName, OutAttachmentIndex);
            if (!Result && Target && Target->Class && Target->Class->ClassDefaultObject) {
                Result = FindClassInReflectedArray(Target->Class->ClassDefaultObject,
                    ArrayPropertyName, ClassName, OutAttachmentIndex);
            }
            if (!Result) Result = SDK::BasicFilesImplUtils::FindClassByName(ClassName);
            if (!Result && AssetPath) {
                Result = SDK::UVictoryCore::BPLoadClass(SDK::FString(AssetPath));
            }
            if (!Result || !Result->IsSubclassOf(SDK::UPrimalItem::StaticClass())) return nullptr;
            return Result;
        }

        bool IsTekStriderRigItem(const SDK::UPrimalItem* Item) {
            if (!IsUsableObject(Item) || !Item->Class) return false;
            const std::string Name = Item->Class->GetName();
            const auto Matches = [&](const auto& Names) {
                return std::any_of(Names.begin(), Names.end(), [&](const char* Candidate) {
                    return EqualsNameIgnoreCase(Name, Candidate);
                });
            };
            return Matches(TekStriderHeadRigClasses) || Matches(TekStriderBodyRigClasses);
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

        bool InventoryHasEquippedClass(SDK::UPrimalInventoryComponent* Inventory,
            SDK::UClass* ItemClass) {
            if (!Inventory || !ItemClass) return false;
            for (SDK::UPrimalItem* Item : Inventory->EquippedItems) {
                if (IsUsableObject(Item) && Item->IsA(ItemClass)) return true;
            }
            for (SDK::UPrimalItem* Item : Inventory->InventoryItems) {
                if (IsUsableObject(Item) && Item->bEquippedItem && Item->IsA(ItemClass)) return true;
            }
            return false;
        }

        SDK::UPrimalItem* AddTekStriderRigItem(SDK::UWorld* World,
            SDK::UPrimalInventoryComponent* Inventory, SDK::UClass* ItemClass) {
            return SDK::UPrimalItem::AddNewItem(
                SDK::TSubclassOf<SDK::UPrimalItem>(ItemClass), Inventory,
                true, true, 1.0f, true, 1, false, 0.0f, false,
                SDK::TSubclassOf<SDK::UPrimalItem>(nullptr), 0.0f, true,
                true, true, false, true, false, World);
        }

        void WriteTekStriderAttachmentState(SDK::APrimalDinoCharacter* Target,
            int32_t HeadAttachmentIndex, int32_t BodyAttachmentIndex) {
            WriteReflectedInt32Value(Target, "head attachment index", HeadAttachmentIndex);
            WriteReflectedInt32Value(Target, "manual head index", HeadAttachmentIndex);
            WriteReflectedInt32Value(Target, "chest attachment index", BodyAttachmentIndex);
            WriteReflectedInt32Value(Target, "manual chest index", BodyAttachmentIndex);
            WriteReflectedBoolValue(Target, "has set initial attachments", true);
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

    void RequestTekStriderRigChange(int HeadRigIndex, int BodyRigIndex) {
        std::scoped_lock Lock(g_TekStriderRigRequest.Mutex);
        if (HeadRigIndex < 0 || HeadRigIndex >= static_cast<int>(TekStriderHeadRigClasses.size()) ||
            BodyRigIndex < 0 || BodyRigIndex >= static_cast<int>(TekStriderBodyRigClasses.size())) {
            g_TekStriderRigRequest.Status = "无效的跨步者配件选择";
            return;
        }
        g_TekStriderRigRequest.HeadRigIndex = HeadRigIndex;
        g_TekStriderRigRequest.BodyRigIndex = BodyRigIndex;
        g_TekStriderRigRequest.Pending = true;
        g_TekStriderRigRequest.Status = "已提交，等待游戏线程应用配件";
    }

    std::string GetTekStriderRigStatus() {
        std::scoped_lock Lock(g_TekStriderRigRequest.Mutex);
        return g_TekStriderRigRequest.Status;
    }

    void ProcessTekStriderRigRequest(SDK::UWorld* World) {
        int HeadSelection = -1;
        int BodySelection = -1;
        {
            std::scoped_lock Lock(g_TekStriderRigRequest.Mutex);
            if (!g_TekStriderRigRequest.Pending) return;
            HeadSelection = g_TekStriderRigRequest.HeadRigIndex;
            BodySelection = g_TekStriderRigRequest.BodyRigIndex;
            g_TekStriderRigRequest.Pending = false;
        }

        if (!World) {
            SetTekStriderRigStatus("世界尚未加载，请进入单机地图后重试");
            return;
        }

        SDK::APrimalDinoCharacter* Target = ResolveTekStriderTarget(World);
        if (!Target) {
            SetTekStriderRigStatus("未找到跨步者：请贴近并瞄准它，或先骑上它");
            return;
        }
        if (!Target->HasAuthority()) {
            SetTekStriderRigStatus("目标没有本地服务器权限；此功能仅支持单机或主机");
            return;
        }

        SDK::UPrimalInventoryComponent* Inventory = Target->MyInventoryComponent;
        if (!IsUsableObject(Inventory)) {
            SetTekStriderRigStatus("目标跨步者的隐藏物品栏尚未初始化，请稍等后重试");
            return;
        }

        int32_t HeadAttachmentIndex = HeadSelection;
        int32_t BodyAttachmentIndex = BodySelection;
        SDK::UClass* HeadClass = ResolveTekStriderRigClass(Target,
            "Head Attachment Classes", TekStriderHeadRigClasses[HeadSelection],
            TekStriderHeadRigPaths[HeadSelection], &HeadAttachmentIndex);
        SDK::UClass* BodyClass = ResolveTekStriderRigClass(Target,
            "Chest Attachment Classes", TekStriderBodyRigClasses[BodySelection],
            TekStriderBodyRigPaths[BodySelection], &BodyAttachmentIndex);
        if (!HeadClass || !BodyClass) {
            SetTekStriderRigStatus("配件资源类未加载；请让一只自然刷新的跨步者进入视野后重试");
            return;
        }

        if (HeadAttachmentIndex < 0) HeadAttachmentIndex = HeadSelection;
        if (BodyAttachmentIndex < 0) BodyAttachmentIndex = BodySelection;
        WriteTekStriderAttachmentState(Target, HeadAttachmentIndex, BodyAttachmentIndex);

        const bool AlreadyHasHead = InventoryHasEquippedClass(Inventory, HeadClass);
        const bool AlreadyHasBody = InventoryHasEquippedClass(Inventory, BodyClass);
        if (AlreadyHasHead && AlreadyHasBody) {
            Inventory->InventoryRefresh();
            SetTekStriderRigStatus("目标已经装备了所选头部和躯干配件");
            return;
        }

        struct ExistingRigItem {
            SDK::FItemNetID ItemID{};
            SDK::UClass* ItemClass = nullptr;
        };
        std::vector<ExistingRigItem> ExistingItems;
        auto CollectRigItems = [&](const SDK::TArray<SDK::UPrimalItem*>& Items) {
            for (SDK::UPrimalItem* Item : Items) {
                if (!IsTekStriderRigItem(Item)) continue;
                const auto Duplicate = std::find_if(ExistingItems.begin(), ExistingItems.end(),
                    [&](const ExistingRigItem& Existing) {
                        return Existing.ItemID.ItemID1 == Item->ItemID.ItemID1 &&
                            Existing.ItemID.ItemID2 == Item->ItemID.ItemID2;
                    });
                if (Duplicate == ExistingItems.end()) {
                    ExistingItems.push_back({ Item->ItemID, Item->Class });
                }
            }
        };
        CollectRigItems(Inventory->EquippedItems);
        CollectRigItems(Inventory->InventoryItems);

        std::vector<SDK::UClass*> RemovedClasses;
        for (const ExistingRigItem& Existing : ExistingItems) {
            if (!Inventory->RemoveItem(Existing.ItemID, false, false, true, false)) {
                for (SDK::UClass* RemovedClass : RemovedClasses) {
                    AddTekStriderRigItem(World, Inventory, RemovedClass);
                }
                Inventory->InventoryRefresh();
                SetTekStriderRigStatus("旧配件无法从隐藏装备栏移除，已恢复原配件");
                return;
            }
            RemovedClasses.push_back(Existing.ItemClass);
        }

        SDK::UPrimalItem* NewHead = AddTekStriderRigItem(World, Inventory, HeadClass);
        SDK::UPrimalItem* NewBody = AddTekStriderRigItem(World, Inventory, BodyClass);
        Inventory->InventoryRefresh();

        const bool HeadEquipped = NewHead && InventoryHasEquippedClass(Inventory, HeadClass);
        const bool BodyEquipped = NewBody && InventoryHasEquippedClass(Inventory, BodyClass);
        if (!HeadEquipped || !BodyEquipped) {
            if (NewHead && IsUsableObject(NewHead)) {
                Inventory->RemoveItem(NewHead->ItemID, false, false, true, false);
            }
            if (NewBody && IsUsableObject(NewBody)) {
                Inventory->RemoveItem(NewBody->ItemID, false, false, true, false);
            }
            for (SDK::UClass* RemovedClass : RemovedClasses) {
                AddTekStriderRigItem(World, Inventory, RemovedClass);
            }
            Inventory->InventoryRefresh();
            SetTekStriderRigStatus("新配件未能进入隐藏装备槽，已回滚原配件");
            return;
        }

        Target->BPNotifyInventoryItemChange(true, NewHead, true);
        Target->BPNotifyInventoryItemChange(true, NewBody, true);
        // Inventory removal/add notifications may update these Blueprint
        // variables, so make the selected saved indices the final write.
        WriteTekStriderAttachmentState(Target, HeadAttachmentIndex, BodyAttachmentIndex);
        Inventory->InventoryRefresh();
        SetTekStriderRigStatus("已完成：跨步者配件已写入隐藏装备栏并保存到当前存档");
    }

    void AutoOpenGachaCrystals(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point lastUseTime = Clock::now() - std::chrono::seconds(1);

        const Clock::time_point now = Clock::now();
        const int32_t IntervalMilliseconds = static_cast<int32_t>(std::lround(
            std::clamp(g_Config::AutoOpenGachaIntervalMs, 10.0f, 1000.0f)));
        const auto UseInterval = std::chrono::milliseconds(IntervalMilliseconds);
        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) {
            g_AutoGachaPullRuntime.Status = "等待人物和世界数据";
            return;
        }

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController || !LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) return;

        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!g_Config::bAutoOpenGachaCrystals) {
            ClearAutoGachaTarget(PC, true);
            g_AutoGachaPullRuntime.RetryAfter.clear();
            g_AutoGachaPullRuntime.Status = "自动打开嘎查水晶未启用";
            return;
        }

        SDK::APrimalCharacter* InventoryOwner = GetPlayerInventoryOwner(PC);
        SDK::UPrimalInventoryComponent* Inventory = InventoryOwner ? InventoryOwner->MyInventoryComponent : nullptr;
        if (!Inventory) {
            g_AutoGachaPullRuntime.Status = "等待人物背包数据";
            return;
        }

        if (SDK::UPrimalItem* Item = FindGachaCrystalInInventory(Inventory)) {
            if (now - lastUseTime >= UseInterval) {
                PC->ServerRequestInventoryUseItem(Inventory, Item->ItemID, Item->SlotIndex);
                g_AutoGachaPullRuntime.Status = std::format(
                    "正在以 {}ms 间隔打开人物背包内的嘎查水晶", IntervalMilliseconds);
                lastUseTime = now;
            }
            return; // Empty the player inventory before pulling another stack.
        }

        ProcessAutoGachaVaultPull(World, PC, Human);
    }

    std::string GetAutoPullGachaStatus() {
        return g_AutoGachaPullRuntime.Status;
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
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
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
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
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

    void TekChestJetNoCooldown(SDK::UWorld* World) {
        if (!g_Config::bTekChestJetNoCooldown) {
            g_TekChestJetStatus = "泰克胸甲喷射无冷却未启用";
            return;
        }

        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!Human || Human->IsDead()) {
            g_TekChestJetStatus = "等待人物数据";
            return;
        }

        SDK::ABuff_TekArmor_C* ChestBuff = nullptr;
        SDK::TArray<SDK::APrimalBuff*> Buffs;
        Human->GetAllBuffs(&Buffs);
        SDK::UClass* TekArmorBuffClass = SDK::ABuff_TekArmor_C::StaticClass();
        for (int32_t Index = 0; Index < Buffs.Num(); ++Index) {
            SDK::APrimalBuff* Buff = Buffs[Index];
            if (!IsUsableObject(Buff) || !TekArmorBuffClass || !Buff->IsA(TekArmorBuffClass)) continue;

            SDK::ABuff_TekArmor_C* TekBuff = static_cast<SDK::ABuff_TekArmor_C*>(Buff);
            std::string Identity = TekBuff->Class->GetName();
            Identity += TekBuff->GetName();
            Identity += TekBuff->GetFullName();
            Identity = NormalizeGachaItemName(std::move(Identity));
            uint8_t TekBuffEquipmentType = 0xFF;
            uint8_t CurrentEquipmentSlot = 0xFF;
            ReadReflectedByteValue(TekBuff, "TekBuffEquipmentType", &TekBuffEquipmentType);
            ReadReflectedByteValue(TekBuff, "currentEquipmentSlot", &CurrentEquipmentSlot);
            const uint8_t ShirtSlot = static_cast<uint8_t>(SDK::EPrimalEquipmentType::Shirt);
            const bool IsShirtSlot = TekBuffEquipmentType == ShirtSlot ||
                CurrentEquipmentSlot == ShirtSlot;
            const bool IsChestIdentity = Identity.find("tekarmorshirt") != std::string::npos ||
                Identity.find("tekshirt") != std::string::npos ||
                Identity.find("tekchest") != std::string::npos ||
                Identity.find("jetpack") != std::string::npos;
            if (IsShirtSlot || IsChestIdentity) {
                ChestBuff = TekBuff;
                break;
            }
        }

        if (!ChestBuff) {
            g_TekChestJetStatus = "未检测到已装备的泰克胸甲 Buff";
            return;
        }

        if (!Human->HasAuthority()) {
            g_TekChestJetStatus =
                "远程服务器：喷射冷却由服务端维护，客户端没有可安全重置的状态";
            return;
        }

        const TekCooldownClearResult Cooldowns = ClearTekSkillCooldowns(Human,
            { "chest", "shirt", "jet", "jetpack", "thruster", "thrust", "boost", "jump" },
            { "chest", "shirt", "jet", "jetpack", "thruster" });
        if (Cooldowns.ClearedCount > 0) {
            g_TekChestJetStatus = std::format("已重置胸甲技能冷却: {}{}",
                Cooldowns.ClearedNames,
                Human->HasAuthority() ? "" : "（服务器可能继续校验）");
            return;
        }

        g_TekChestJetStatus = Cooldowns.ActiveNames.empty() ?
            "胸甲可正常触发；尚未捕获到喷射技能冷却，请先双击空格使用一次" :
            std::format("未匹配胸甲冷却；当前活动冷却: {}", Cooldowns.ActiveNames);
    }

    std::string GetTekChestJetStatus() {
        return g_TekChestJetStatus;
    }

    void TekGloveDashNoCooldown(SDK::UWorld* World) {
        if (!g_Config::bTekGloveDashNoCooldown) {
            g_TekGloveDashStatus = "泰克手套冲刺无冷却未启用";
            return;
        }

        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!Human || Human->IsDead()) {
            g_TekGloveDashStatus = "等待人物数据";
            return;
        }

        SDK::ABuff_TekArmor_C* GloveBuff = nullptr;
        SDK::TArray<SDK::APrimalBuff*> Buffs;
        Human->GetAllBuffs(&Buffs);
        SDK::UClass* TekArmorBuffClass = SDK::ABuff_TekArmor_C::StaticClass();
        for (int32_t Index = 0; Index < Buffs.Num(); ++Index) {
            SDK::APrimalBuff* Buff = Buffs[Index];
            if (!IsUsableObject(Buff) || !TekArmorBuffClass || !Buff->IsA(TekArmorBuffClass)) continue;

            SDK::ABuff_TekArmor_C* TekBuff = static_cast<SDK::ABuff_TekArmor_C*>(Buff);
            std::string Identity = TekBuff->Class->GetName();
            Identity += TekBuff->GetName();
            Identity += TekBuff->GetFullName();
            Identity = NormalizeGachaItemName(std::move(Identity));
            uint8_t TekBuffEquipmentType = 0xFF;
            uint8_t CurrentEquipmentSlot = 0xFF;
            ReadReflectedByteValue(TekBuff, "TekBuffEquipmentType", &TekBuffEquipmentType);
            ReadReflectedByteValue(TekBuff, "currentEquipmentSlot", &CurrentEquipmentSlot);
            const uint8_t GloveSlot = static_cast<uint8_t>(SDK::EPrimalEquipmentType::Gloves);
            const bool IsGloveSlot = TekBuffEquipmentType == GloveSlot ||
                CurrentEquipmentSlot == GloveSlot;
            const bool IsGloveIdentity = Identity.find("tekarmorglove") != std::string::npos ||
                Identity.find("tekglove") != std::string::npos ||
                Identity.find("tekgauntlet") != std::string::npos ||
                Identity.find("tekfist") != std::string::npos;
            if (IsGloveSlot || IsGloveIdentity) {
                GloveBuff = TekBuff;
                break;
            }
        }

        if (!GloveBuff) {
            g_TekGloveDashStatus = "未检测到已装备的泰克手套 Buff";
            return;
        }

        if (!Human->HasAuthority()) {
            g_TekGloveDashStatus =
                "远程服务器：拳套冷却由服务端维护，客户端没有可安全重置的状态";
            return;
        }

        bool ResetMeleeTimer = false;
        SDK::UObject* FistWeapon = ReadReflectedObjectValue(GloveBuff, "FistWeapon");
        if (IsUsableObject(FistWeapon) && FistWeapon->IsA(SDK::AShooterWeapon::StaticClass())) {
            // This is the native absolute timestamp used by melee weapons. It is
            // safe to move into the past; do not touch animation/window lengths.
            ResetMeleeTimer = ForcePastNumericProperty(FistWeapon, "NextAllowedMeleeTime");
        }

        const TekCooldownClearResult Cooldowns = ClearTekSkillCooldowns(Human,
            { "glove", "gauntlet", "fist", "punch", "dash", "charge", "melee" },
            { "glove", "gauntlet", "tekfist" });
        if (ResetMeleeTimer || Cooldowns.ClearedCount > 0) {
            std::string Details;
            if (ResetMeleeTimer) Details = "NextAllowedMeleeTime";
            if (Cooldowns.ClearedCount > 0) {
                if (!Details.empty()) Details += ", ";
                Details += Cooldowns.ClearedNames;
            }
            g_TekGloveDashStatus = std::format("已重置手套冲刺计时: {}{}", Details,
                Human->HasAuthority() ? "" : "（服务器可能继续校验）");
            return;
        }

        g_TekGloveDashStatus = Cooldowns.ActiveNames.empty() ?
            "已识别泰克手套，但拳套对象尚未生成；请先蓄力冲刺一次" :
            std::format("未匹配手套冷却；当前活动冷却: {}", Cooldowns.ActiveNames);
    }

    std::string GetTekGloveDashStatus() {
        return g_TekGloveDashStatus;
    }

    void AutoFishingQTE(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        struct ExternalKeyState {
            bool Active = false;
            SDK::FKey Key{};
            Clock::time_point PressedAt = Clock::time_point::min();
        };
        static Clock::time_point LastHandledTime = Clock::time_point::min();
        static int32_t LastWidgetIndex = -1;
        static int32_t LastKeyIndex = -1;
        static SDK::FName LastKeyName{};
        static ExternalKeyState ExternalKey;
        static bool SawQTESequence = false;
        static Clock::time_point LastPromptSeen = Clock::time_point::min();
        static Clock::time_point LastRecast = Clock::time_point::min();
        constexpr auto HandleInterval = std::chrono::milliseconds(50);
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
            g_MDX12::g_InputState::g_bypassOutBodyHotkeyForFishing.store(false);
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
        SDK::UObject* PromptSource = FindActiveFishingPrompt(
            Now, &CurrentKey, &CurrentKeyIndex);

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
        RefreshAutoCraftCatalog(World);
        return g_AutoCraftRuntime.Catalog;
    }

    void SelectAutoCraftStation(int InventoryObjectIndex) {
        if (g_AutoCraftRuntime.SelectedInventoryObjectIndex != InventoryObjectIndex) {
            g_AutoCraftRuntime.SelectedInventoryObjectIndex = InventoryObjectIndex;
            g_AutoCraftRuntime.SelectedItemObjectIndex = -1;
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

    void SelectAutoCraftRecipe(int ItemObjectIndex) {
        g_AutoCraftRuntime.SelectedItemObjectIndex = ItemObjectIndex;
        g_AutoCraftRuntime.Status = ItemObjectIndex >= 0 ?
            "已就绪；开启自动制作后将按服务器规则循环请求" : "请选择印痕/图纸";
    }

    int GetSelectedAutoCraftStation() {
        return g_AutoCraftRuntime.SelectedInventoryObjectIndex;
    }

    int GetSelectedAutoCraftRecipe() {
        return g_AutoCraftRuntime.SelectedItemObjectIndex;
    }

    std::string GetAutoCraftStatus() {
        return g_AutoCraftRuntime.Status;
    }

    void AutoCraft(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        RefreshAutoCraftCatalog(World);
        if (!g_Config::bAutoCraft) return;

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        SDK::UObject* InventoryObject = ResolveObjectAtIndex(
            g_AutoCraftRuntime.SelectedInventoryObjectIndex);
        SDK::UObject* ItemObject = ResolveObjectAtIndex(g_AutoCraftRuntime.SelectedItemObjectIndex);
        SDK::UClass* InventoryClass = SDK::UPrimalInventoryComponent::StaticClass();
        SDK::UClass* ItemClass = SDK::UPrimalItem::StaticClass();
        if (!PC || !Human || !InventoryObject || !ItemObject || !InventoryClass || !ItemClass ||
            !InventoryObject->IsA(InventoryClass) || !ItemObject->IsA(ItemClass)) {
            g_AutoCraftRuntime.Status = "选择已失效，请重新选择工作台和印痕/图纸";
            return;
        }

        SDK::UPrimalInventoryComponent* Inventory =
            static_cast<SDK::UPrimalInventoryComponent*>(InventoryObject);
        SDK::UPrimalItem* Item = static_cast<SDK::UPrimalItem*>(ItemObject);

        SDK::AActor* InventoryOwner = Inventory->GetOwner();
        const float DistanceMeters = InventoryOwner ? Human->GetDistanceTo(InventoryOwner) * 0.01f : INFINITY;
        if (!std::isfinite(DistanceMeters) ||
            DistanceMeters > std::clamp(g_Config::AutoCraftRangeMeters, 2.0f, 100.0f)) {
            g_AutoCraftRuntime.Status = "工作台超出设置范围";
            return;
        }

        SDK::UObject** ItemObjects = nullptr;
        int32_t ItemCount = 0;
        bool ItemBelongsToInventory = false;
        if (ReadReflectedObjectArray(Inventory, "InventoryItems", &ItemObjects, &ItemCount)) {
            for (int32_t Index = 0; Index < ItemCount && Index < 2048; ++Index) {
                if (ItemObjects[Index] == Item) {
                    ItemBelongsToInventory = true;
                    break;
                }
            }
        }
        if (!ItemBelongsToInventory) {
            g_AutoCraftRuntime.Status = "所选印痕已不在该工作台，请重新选择";
            return;
        }

        const Clock::time_point Now = Clock::now();
        if (Now < g_AutoCraftRuntime.NextCraftRequest) return;
        const float Interval = std::clamp(g_Config::AutoCraftIntervalSeconds, 0.25f, 10.0f);
        g_AutoCraftRuntime.NextCraftRequest = Now + std::chrono::milliseconds(
            static_cast<int64_t>(Interval * 1000.0f));

        constexpr int32_t TargetQueueQuantity = 100;
        const int32_t CurrentQueueQuantity = std::clamp(
            Item->GetCraftQueueCount(), 0, TargetQueueQuantity);
        if (CurrentQueueQuantity >= TargetQueueQuantity) {
            g_AutoCraftRuntime.Status = "当前印痕制作队列已达到 100";
            return;
        }
        if (!Inventory->IsCraftingAllowed(Item) || !Item->CanCraft(PC) ||
            !Item->MeetBlueprintCraftingRequirements(Inventory, 1, PC, true, true, true)) {
            g_AutoCraftRuntime.Status = "服务器条件未满足（材料、权限或队列）";
            return;
        }

        int32_t ItemID1 = 0;
        int32_t ItemID2 = 0;
        Item->BPGetItemID(&ItemID1, &ItemID2);
        if (ItemID1 == 0 && ItemID2 == 0) {
            g_AutoCraftRuntime.Status = "无法读取所选印痕的服务器物品 ID";
            return;
        }

        SDK::FItemNetID ItemID{};
        ItemID.ItemID1 = static_cast<uint32_t>(ItemID1);
        ItemID.ItemID2 = static_cast<uint32_t>(ItemID2);
        const int32_t RequestsToSend = TargetQueueQuantity - CurrentQueueQuantity;
        for (int32_t RequestIndex = 0; RequestIndex < RequestsToSend; ++RequestIndex) {
            // The game exposes a one-item reliable RPC rather than a quantity
            // parameter. Repeat the normal request and let the server validate
            // materials, permissions and queue capacity for every entry.
            PC->ServerCraftItem(Inventory, ItemID);
        }
        g_AutoCraftRuntime.Status = "已提交 " + std::to_string(RequestsToSend) +
            " 个制作请求；目标队列数量为 100";
    }

    std::string GetAutoImprintStatus() {
        return g_AutoImprintStatus;
    }

    void AutoImprint(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point NextScan = Clock::time_point::min();
        static std::unordered_map<int32_t, Clock::time_point> NextWalkFollowRequest;
        static std::unordered_map<int32_t, Clock::time_point> NextCareRequest;
        static std::unordered_map<int32_t, Clock::time_point> NextFoodSlotRequest;
        static std::unordered_map<int32_t, Clock::time_point> CareTargetSkipUntil;
        static int32_t LockedCareDinoIndex = -1;
        static Clock::time_point LockedCareUntil = Clock::time_point::min();
        if (!g_Config::bAutoImprint) {
            g_AutoImprintStatus = "等待附近幼龙提出留痕需求";
            NextWalkFollowRequest.clear();
            NextCareRequest.clear();
            NextFoodSlotRequest.clear();
            CareTargetSkipUntil.clear();
            LockedCareDinoIndex = -1;
            LockedCareUntil = Clock::time_point::min();
            return;
        }

        const Clock::time_point Now = Clock::now();
        if (Now < NextScan) return;
        NextScan = Now + std::chrono::milliseconds(1000);

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        SDK::UClass* DinoClass = SDK::APrimalDinoCharacter::StaticClass();
        if (!PC || !Human || !World || !DinoClass) {
            g_AutoImprintStatus = "等待人物和世界数据";
            return;
        }

        int32_t PlayerTeam = 0;
        if (!ReadReflectedInt32Value(Human, "TargetingTeam", &PlayerTeam)) {
            g_AutoImprintStatus = "无法读取玩家部落信息";
            return;
        }

        SDK::TArray<SDK::AActor*> Actors;
        SDK::UGameplayStatics::GetAllActorsOfClass(World, DinoClass, &Actors);
        if (Actors.Num() < 0 || Actors.Num() > 100000) return;

        const double NetworkTime = SDK::UVictoryCore::GetNetworkTimeInSeconds(World);
        if (!std::isfinite(NetworkTime) || NetworkTime <= 0.0) {
            g_AutoImprintStatus = "等待服务器时间同步";
            return;
        }
        const float MaximumRange = std::clamp(g_Config::AutoImprintRangeMeters, 2.0f, 30.0f);
        SDK::APrimalDinoCharacter* BestDino = nullptr;
        uint8_t BestCuddleType = 0;
        float BestDistance = INFINITY;
        SDK::APrimalDinoCharacter* LockedDino = nullptr;
        uint8_t LockedCuddleType = 0;
        float LockedDistance = INFINITY;
        std::vector<SDK::APrimalDinoCharacter*> WalkDinos;

        if (LockedCareDinoIndex >= 0 && Now >= LockedCareUntil) {
            CareTargetSkipUntil[LockedCareDinoIndex] = Now + std::chrono::seconds(8);
            LockedCareDinoIndex = -1;
            LockedCareUntil = Clock::time_point::min();
        }
        std::erase_if(CareTargetSkipUntil,
            [Now](const auto& Pair) { return Pair.second <= Now; });

        for (int32_t Index = 0; Index < Actors.Num(); ++Index) {
            SDK::AActor* Actor = Actors[Index];
            if (!IsUsableObject(Actor) || !Actor->IsA(DinoClass)) continue;
            SDK::APrimalDinoCharacter* Dino = static_cast<SDK::APrimalDinoCharacter*>(Actor);

            double BabyAge = 0.0;
            double NextCuddleTime = 0.0;
            uint8_t CuddleType = 0;
            if (!ReadReflectedNumberValue(Dino, "BabyAge", &BabyAge) || BabyAge <= 0.0 || BabyAge >= 1.0 ||
                !ReadReflectedNumberValue(Dino, "BabyNextCuddleTime", &NextCuddleTime) ||
                NextCuddleTime <= 0.0 || NextCuddleTime > NetworkTime + 0.25 ||
                !ReadReflectedByteValue(Dino, "BabyCuddleType", &CuddleType) || CuddleType > 2 ||
                !Dino->IsAlliedWithOtherTeam(PlayerTeam)) {
                continue;
            }

            const float Distance = Human->GetDistanceTo(Dino) * 0.01f;
            if (!std::isfinite(Distance) || Distance > MaximumRange) continue;
            if (CuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::WALK)) {
                WalkDinos.push_back(Dino);
                continue;
            }

            // Keep one food/pet target stable while its requested hotbar item
            // replicates. Re-selecting the nearest baby every second makes
            // several babies continuously replace one another's food and none
            // of their interactions can ever become enabled.
            if (Dino->Index == LockedCareDinoIndex) {
                LockedDino = Dino;
                LockedCuddleType = CuddleType;
                LockedDistance = Distance;
                continue;
            }
            const auto SkipIt = CareTargetSkipUntil.find(Dino->Index);
            if (SkipIt != CareTargetSkipUntil.end() && Now < SkipIt->second) continue;
            if (Distance >= BestDistance) continue;
            BestDino = Dino;
            BestCuddleType = CuddleType;
            BestDistance = Distance;
        }

        if (LockedDino) {
            BestDino = LockedDino;
            BestCuddleType = LockedCuddleType;
            BestDistance = LockedDistance;
        }
        else {
            LockedCareDinoIndex = -1;
            LockedCareUntil = Clock::time_point::min();
            if (BestDino && BestCuddleType != static_cast<uint8_t>(SDK::EBabyCuddleType::WALK)) {
                LockedCareDinoIndex = BestDino->Index;
                LockedCareUntil = Now + std::chrono::seconds(20);
            }
        }

        int32_t WalkAlreadyFollowing = 0;
        int32_t WalkFollowRequests = 0;
        for (SDK::APrimalDinoCharacter* WalkDino : WalkDinos) {
            if (!IsUsableObject(WalkDino)) continue;
            if (WalkDino->GetTamedFollowTarget() == Human) {
                ++WalkAlreadyFollowing;
                continue;
            }

            Clock::time_point& NextRequest = NextWalkFollowRequest[WalkDino->Index];
            if (Now < NextRequest) continue;
            NextRequest = Now + std::chrono::seconds(3);
            Human->ServerCallFollowOne(WalkDino);
            ++WalkFollowRequests;
        }

        std::string WalkStatusSuffix;
        if (!WalkDinos.empty()) {
            WalkStatusSuffix = std::format("；散步幼龙 {} 只，已跟随 {} 只，本次发送跟随 {} 只",
                WalkDinos.size(), WalkAlreadyFollowing, WalkFollowRequests);
        }

        if (!BestDino) {
            g_AutoImprintStatus = WalkDinos.empty()
                ? "范围内没有已到时间且允许照料的幼龙"
                : "已让散步幼龙跟随；请让人物实际移动以完成留痕" + WalkStatusSuffix;
            return;
        }

        // ServerMultiUse performs the same distance validation as pressing E.
        // Keep the wider configured range for discovery, but never claim that
        // a remote request was accepted when the baby is outside use range.
        constexpr float MaximumCareUseRangeMeters = 5.0f;
        if (BestDistance > MaximumCareUseRangeMeters) {
            g_AutoImprintStatus = std::format(
                "已发现{}留痕，但距离 {:.1f}m；请靠近到 5m 内",
                BestCuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::FOOD)
                    ? "喂食" : "抱抱/抚摸",
                BestDistance) + WalkStatusSuffix;
            return;
        }

        SDK::TArray<SDK::FMultiUseEntry> Entries;
        BestDino->GetMultiUseEntries(PC, &Entries, -1);
        if (Entries.Num() < 0 || Entries.Num() > 2048) {
            g_AutoImprintStatus = "服务器返回的幼龙交互列表无效";
            return;
        }

        int32_t SelectedUseIndex = -1;
        SDK::UObject* SelectedUseObject = BestDino;
        bool SelectedClientSideOnly = false;
        bool SelectedEntryDisabled = false;
        bool SelectedEntryHidden = false;
        std::string MatchingEntryText;
        int32_t DisabledUseIndex = -1;
        SDK::UObject* DisabledUseObject = BestDino;
        bool DisabledClientSideOnly = false;
        bool DisabledEntryHidden = false;
        std::string DisabledEntryText;
        std::vector<std::string> EntryDiagnostics;
        EntryDiagnostics.reserve(std::min(Entries.Num(), 6));
        for (int32_t EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex) {
            const SDK::FMultiUseEntry& Entry = Entries[EntryIndex];
            const std::string RawText = Entry.UseString.ToString();
            const std::string Text = NormalizeGachaItemName(RawText);
            if (EntryDiagnostics.size() < 6 && !RawText.empty()) {
                EntryDiagnostics.push_back(std::format("{}:{}{}{}{}", Entry.UseIndex,
                    RawText.substr(0, 48), Entry.bDisableUse ? "[禁用]" : "",
                    Entry.bHideFromUI ? "[隐藏]" : "",
                    Entry.bClientSideOnly ? "[本地]" : ""));
            }

            bool Matches = false;
            if (BestCuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::PET)) {
                Matches = Text.find("cuddle") != std::string::npos || Text.find("pet") != std::string::npos ||
                    Text.find("care") != std::string::npos || Text.find("hug") != std::string::npos ||
                    Text.find("imprint") != std::string::npos || RawText.find("抱") != std::string::npos ||
                    RawText.find("抚摸") != std::string::npos || RawText.find("亲近") != std::string::npos ||
                    RawText.find("照料") != std::string::npos;
            }
            else if (BestCuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::FOOD)) {
                Matches = Text.find("feed") != std::string::npos || Text.find("food") != std::string::npos ||
                    Text.find("wants") != std::string::npos || Text.find("request") != std::string::npos ||
                    RawText.find("喂") != std::string::npos || RawText.find("想要") != std::string::npos ||
                    RawText.find("食物") != std::string::npos;
            }
            if (!Matches) continue;

            if (Entry.bDisableUse) {
                // bDisableUse controls the HUD entry, not the authoritative
                // ServerMultiUse RPC. Keep it as a fallback and let the server
                // perform the final distance/item/ownership validation. This is
                // required for baby food entries such as UseIndex 139, which ASA
                // can report as both hidden and disabled on the owning client.
                if (DisabledUseIndex < 0) {
                    DisabledUseIndex = Entry.UseIndex;
                    if (Entry.ForComponent && IsUsableObject(Entry.ForComponent)) {
                        DisabledUseObject = Entry.ForComponent;
                    }
                    DisabledClientSideOnly = Entry.bClientSideOnly;
                    DisabledEntryHidden = Entry.bHideFromUI;
                    DisabledEntryText = RawText;
                }
                continue;
            }

            SelectedUseIndex = Entry.UseIndex;
            if (Entry.ForComponent && IsUsableObject(Entry.ForComponent)) {
                SelectedUseObject = Entry.ForComponent;
            }
            SelectedClientSideOnly = Entry.bClientSideOnly;
            SelectedEntryHidden = Entry.bHideFromUI;
            MatchingEntryText = RawText;
            break;
        }

        if (SelectedUseIndex < 0 && DisabledUseIndex >= 0) {
            SelectedUseIndex = DisabledUseIndex;
            SelectedUseObject = DisabledUseObject;
            SelectedClientSideOnly = DisabledClientSideOnly;
            SelectedEntryDisabled = true;
            SelectedEntryHidden = DisabledEntryHidden;
            MatchingEntryText = std::move(DisabledEntryText);
        }

        if (SelectedUseIndex < 0) {
            std::string DiagnosticText;
            for (const std::string& EntryText : EntryDiagnostics) {
                if (!DiagnosticText.empty()) DiagnosticText += " | ";
                DiagnosticText += EntryText;
            }
            g_AutoImprintStatus = "未找到对应的" + std::string(
                BestCuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::FOOD)
                    ? "喂食" : "抱抱/抚摸") + "交互项";
            if (!DiagnosticText.empty()) g_AutoImprintStatus += "；交互列表 " + DiagnosticText;
            g_AutoImprintStatus += WalkStatusSuffix;
            return;
        }

        bool IsImprintOwner = BestDino->IsImprintPlayer(Human);
        bool HasServerCuddlePolicy = false;
        bool ServerAllowsAnyone = false;
        SDK::AGameStateBase* BaseGameState = SDK::UGameplayStatics::GetGameState(World);
        SDK::UClass* ShooterGameStateClass = SDK::AShooterGameState::StaticClass();
        if (IsUsableObject(BaseGameState) && ShooterGameStateClass &&
            BaseGameState->IsA(ShooterGameStateClass)) {
            auto* ShooterGameState = static_cast<SDK::AShooterGameState*>(BaseGameState);
            HasServerCuddlePolicy = true;
            ServerAllowsAnyone = ShooterGameState->bAllowAnyoneBabyImprintCuddle;
        }
        if (SelectedEntryDisabled && HasServerCuddlePolicy &&
            !ServerAllowsAnyone && !IsImprintOwner) {
            g_AutoImprintStatus = "当前人物不是该幼龙的留痕绑定者，且服务器未开启任何人均可留痕" +
                WalkStatusSuffix;
            return;
        }

        std::string RequiredFood;
        std::string FoodTendingFailReason;
        if (BestCuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::FOOD)) {
            SDK::TSubclassOf<SDK::UPrimalItem> Food = BestDino->GetBabyCuddleFood();
            SDK::UClass* FoodClass = Food.Get();
            if (!IsUsableObject(FoodClass)) {
                g_AutoImprintStatus = "已识别喂食任务，但服务器尚未同步所需食物" +
                    WalkStatusSuffix;
                return;
            }

            RequiredFood = FoodClass->GetName();
            std::string FriendlyFoodName = RequiredFood;
            const std::string FoodIdentity = NormalizeGachaItemName(RequiredFood);
            if (FoodIdentity.find("cookedmeatjerky") != std::string::npos) {
                FriendlyFoodName = "熟肉干";
            }
            else if (FoodIdentity.find("cookedfish") != std::string::npos ||
                FoodIdentity.find("cookedmeatfish") != std::string::npos ||
                FoodIdentity.find("fishmeatcooked") != std::string::npos) {
                FriendlyFoodName = "熟鱼肉";
            }
            else if (FoodIdentity.find("mejoberry") != std::string::npos) {
                FriendlyFoodName = "紫红色浆果";
            }
            else if (FoodIdentity.find("cookedmeat") != std::string::npos) {
                FriendlyFoodName = "熟肉";
            }

            SDK::UPrimalInventoryComponent* Inventory = Human->MyInventoryComponent;
            if (!IsUsableObject(Inventory)) {
                g_AutoImprintStatus = "等待人物背包同步；所需食物 " + FriendlyFoodName;
                return;
            }

            SDK::UPrimalItem* FoodItem = nullptr;
            const int32_t InventoryCount = Inventory->InventoryItems.Num();
            if (InventoryCount >= 0 && InventoryCount <= 100000) {
                for (int32_t ItemIndex = 0; ItemIndex < InventoryCount; ++ItemIndex) {
                    SDK::UPrimalItem* Item = Inventory->InventoryItems[ItemIndex];
                    if (!IsUsableObject(Item) || !Item->Class || Item->ItemQuantity <= 0 ||
                        Item->bIsBlueprint || Item->bIsEngram) {
                        continue;
                    }
                    if (Item->IsA(FoodClass)) {
                        FoodItem = Item;
                        break;
                    }
                }
            }
            if (!FoodItem) {
                g_AutoImprintStatus = "人物背包缺少留痕食物：" + FriendlyFoodName +
                    "（" + RequiredFood + "）" + WalkStatusSuffix;
                return;
            }

            const int32_t SlotCount = Inventory->ItemSlots.Num();
            const int32_t LastHotbarSlot = SlotCount > 0 && SlotCount <= 128
                ? SlotCount - 1 : std::max(0, Inventory->NumSlots - 1);
            SDK::UPrimalItem* LastSlotItem = SlotCount > LastHotbarSlot && LastHotbarSlot >= 0
                ? Inventory->ItemSlots[LastHotbarSlot] : nullptr;
            const bool FoodIsInLastSlot = LastSlotItem == FoodItem ||
                FoodItem->SlotIndex == LastHotbarSlot;
            if (!FoodIsInLastSlot) {
                Clock::time_point& NextSlotRequest = NextFoodSlotRequest[BestDino->Index];
                if (Now >= NextSlotRequest) {
                    Inventory->ServerAddItemToSlot(FoodItem->ItemID, LastHotbarSlot, true);
                    NextSlotRequest = Now + std::chrono::milliseconds(1250);
                }
                if (BestDino->Index == LockedCareDinoIndex) {
                    LockedCareUntil = Now + std::chrono::seconds(20);
                }
                g_AutoImprintStatus = std::format(
                    "正在把留痕食物 {} 放入快捷栏末格（第{}格）",
                    FriendlyFoodName, LastHotbarSlot + 1) + WalkStatusSuffix;
                return;
            }

            FoodTendingFailReason = FoodItem->GetTendingFailReason(PC).ToString();
        }

        Clock::time_point& NextRequest = NextCareRequest[BestDino->Index];
        if (Now < NextRequest) {
            g_AutoImprintStatus = "等待服务器确认留痕请求" + WalkStatusSuffix;
            return;
        }
        NextRequest = Now + std::chrono::milliseconds(2500);
        if (BestDino->Index == LockedCareDinoIndex) {
            LockedCareUntil = Now + std::chrono::seconds(12);
        }

        if (SelectedClientSideOnly) {
            // Client-side entries must be executed through the matching local
            // branch. Sending them as a server RPC is intentionally ignored.
            PC->ClientDoMultiUse(SelectedUseObject, SelectedUseIndex);
        }
        else {
            PC->ServerMultiUse(SelectedUseObject, SelectedUseIndex);
        }

        if (BestCuddleType == static_cast<uint8_t>(SDK::EBabyCuddleType::FOOD)) {
            g_AutoImprintStatus = std::format(
                "已发送喂食留痕：UseIndex {}{}{}{}{}{}{}",
                SelectedUseIndex,
                RequiredFood.empty() ? "" : "；所需物品 ", RequiredFood,
                SelectedEntryDisabled ? "；客户端标记禁用，等待服务器验证" : "",
                SelectedEntryHidden ? "；隐藏交互" : "",
                FoodTendingFailReason.empty() ? "" : "；物品条件：",
                FoodTendingFailReason) + WalkStatusSuffix;
        }
        else {
            g_AutoImprintStatus = std::format(
                "已发送抱抱/抚摸留痕：UseIndex {}{}",
                SelectedUseIndex,
                SelectedClientSideOnly ? "（本地交互）" : "（服务器交互）") +
                WalkStatusSuffix;
        }
    }

    void AutoGrindHelicoprionInventory(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;

        struct QueueEntry {
            SDK::FItemNetID ItemID{};
            std::string Name;
            int32_t EstimatedProcessSeconds = 15;
            int32_t RequestAttempts = 0;
        };
        struct AutoGrindState {
            SDK::UPrimalInventoryComponent* TargetInventory = nullptr;
            int32_t TargetObjectIndex = -1;
            std::vector<QueueEntry> Queue;
            size_t CurrentIndex = 0;
            int32_t ExcludedItems = 0;
            int32_t ClientFilterFallbackItems = 0;
            int32_t ScanAttempts = 0;
            int32_t SnapshotInventoryFrame = -1;
            bool SnapshotReady = false;
            bool AwaitingAcceptance = false;
            bool AcceptanceFailed = false;
            Clock::time_point ScanAt = Clock::time_point::min();
            Clock::time_point AcceptanceDeadline = Clock::time_point::min();
            Clock::time_point RetryAt = Clock::time_point::min();
        };
        static AutoGrindState State;

        auto IsHelicoprionInventory = [](SDK::UPrimalInventoryComponent* Inventory) {
            if (!IsUsableObject(Inventory) || !Inventory->Class) return false;
            const std::string InventoryClassName = NormalizeGachaItemName(Inventory->Class->GetName());
            if (InventoryClassName.find("dinotamedinventorycomponenthelicoprion") == std::string::npos) {
                return false;
            }

            SDK::AActor* Owner = Inventory->GetOwner();
            if (!IsUsableObject(Owner) || !Owner->Class) return false;
            const std::string OwnerClassName = NormalizeGachaItemName(Owner->Class->GetName());
            return OwnerClassName.find("helicoprion") != std::string::npos;
        };

        auto ClearTarget = [&]() {
            State = AutoGrindState{};
        };

        if (!g_Config::bAutoGrindHelicoprionInventory) {
            ClearTarget();
            g_AutoGrindHelicoprionStatus = "未启用";
            return;
        }

        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!PC || !Human) {
            ClearTarget();
            g_AutoGrindHelicoprionStatus = "等待本地人物数据";
            return;
        }

        const Clock::time_point Now = Clock::now();

        // Opening the inventory once selects the exact shark. Keep the component
        // cached after Helicoprion closes its inventory UI on the first grind.
        SDK::UPrimalInventoryComponent* ViewingInventory = PC->GetViewingInventory();
        if (IsHelicoprionInventory(ViewingInventory) &&
            (State.TargetInventory != ViewingInventory ||
                State.TargetObjectIndex != ViewingInventory->Index)) {
            ClearTarget();
            State.TargetInventory = ViewingInventory;
            State.TargetObjectIndex = ViewingInventory->Index;
            State.ScanAt = Now + std::chrono::milliseconds(600);
            g_AutoGrindHelicoprionStatus = "已锁定旋齿鲨，等待背包物品同步";
        }

        SDK::UPrimalInventoryComponent* Target = ResolveCachedObject(
            State.TargetInventory, State.TargetObjectIndex);
        if (!Target || !IsHelicoprionInventory(Target)) {
            ClearTarget();
            g_AutoGrindHelicoprionStatus = "请先打开目标旋齿鲨背包一次以锁定目标";
            return;
        }

        SDK::AActor* TargetOwner = Target->GetOwner();
        const float DistanceMeters = TargetOwner ? Human->GetDistanceTo(TargetOwner) * 0.01f : INFINITY;
        if (!std::isfinite(DistanceMeters) || DistanceMeters > 30.0f) {
            g_AutoGrindHelicoprionStatus = "已记住旋齿鲨，但距离超过 30 米；靠近后会继续";
            return;
        }

        // GetViewingInventory can become valid before the remote item list and
        // the Blueprint grind predicate have finished updating. Taking the
        // snapshot on that first frame made valid equipment (notably shovels)
        // look ungrindable for the lifetime of the lock.
        if (!State.SnapshotReady) {
            if (Now < State.ScanAt) {
                g_AutoGrindHelicoprionStatus = "已锁定旋齿鲨，等待背包物品同步";
                return;
            }

            State.Queue.clear();
            State.CurrentIndex = 0;
            State.ExcludedItems = 0;
            State.ClientFilterFallbackItems = 0;
            State.RetryAt = Clock::time_point::min();

            const int32_t ItemCount = std::min(Target->InventoryItems.Num(), 2048);
            std::unordered_set<uint64_t> SeenItemIDs;
            for (int32_t ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex) {
                SDK::UPrimalItem* Item = Target->InventoryItems[ItemIndex];
                if (!IsUsableObject(Item) || !Item->Class || Item->ItemQuantity <= 0) continue;

                // Crafting-capable inventories also replicate invisible default
                // engrams into InventoryItems. They are not physical contents of
                // the right-hand inventory panel and must never enter a grind
                // snapshot (the first one is often displayed as Simple Pistol).
                bool IsEngram = Item->bIsEngram;
                bool IsHiddenFromInventory = Item->bHideFromInventoryDisplay;
                bool IsHiddenFromRemoteInventory = Item->bHideFromRemoteInventoryDisplay;
                bool ReflectedFlag = false;
                if (ReadReflectedBoolValue(Item, "bIsEngram", &ReflectedFlag)) {
                    IsEngram = ReflectedFlag;
                }
                if (ReadReflectedBoolValue(Item, "bHideFromInventoryDisplay", &ReflectedFlag)) {
                    IsHiddenFromInventory = ReflectedFlag;
                }
                if (ReadReflectedBoolValue(Item, "bHideFromRemoteInventoryDisplay", &ReflectedFlag)) {
                    IsHiddenFromRemoteInventory = ReflectedFlag;
                }
                if (IsEngram || IsHiddenFromInventory || IsHiddenFromRemoteInventory) {
                    ++State.ExcludedItems;
                    continue;
                }

                const bool ClientAllowsGrinding = Target->CanGrindItem(Item);
                const bool IsCraftedItemType =
                    Item->MyItemType == SDK::EPrimalItemType::Equipment ||
                    Item->MyItemType == SDK::EPrimalItemType::Weapon ||
                    Item->MyItemType == SDK::EPrimalItemType::Structure ||
                    Item->MyItemType == SDK::EPrimalItemType::WeaponAttachment;
                const bool HasCraftingProvenance =
                    Item->bUseItemDurability || Item->bIsBlueprint ||
                    Item->BaseCraftingResourceRequirements.Num() > 0 ||
                    Item->CustomResourceRequirements.Num() > 0;
                const bool CanUseServerValidatedFallback =
                    IsCraftedItemType && HasCraftingProvenance &&
                    !Item->bIsItemSkin &&
                    !Item->bIsAbstractItem && !Item->bIsMisssionItem;

                if (!ClientAllowsGrinding && !CanUseServerValidatedFallback) {
                    ++State.ExcludedItems;
                    continue;
                }

                // The replicated member is already the exact identifier needed
                // by the RPC. BPGetItemID is retained only as a compatibility
                // fallback for unusual mod items whose member has not replicated.
                SDK::FItemNetID ItemID = Item->ItemID;
                if (ItemID.ItemID1 == 0 && ItemID.ItemID2 == 0) {
                    int32_t ItemID1 = 0;
                    int32_t ItemID2 = 0;
                    Item->BPGetItemID(&ItemID1, &ItemID2);
                    ItemID.ItemID1 = static_cast<uint32_t>(ItemID1);
                    ItemID.ItemID2 = static_cast<uint32_t>(ItemID2);
                }
                if (ItemID.ItemID1 == 0 && ItemID.ItemID2 == 0) {
                    ++State.ExcludedItems;
                    continue;
                }
                const uint64_t ItemIDKey =
                    (static_cast<uint64_t>(ItemID.ItemID1) << 32) |
                    static_cast<uint64_t>(ItemID.ItemID2);
                if (!SeenItemIDs.insert(ItemIDKey).second) continue;

                QueueEntry Entry;
                Entry.ItemID = ItemID;
                Entry.Name = Item->GetItemShortName().ToString();
                if (Entry.Name.empty() || Entry.Name == "None") {
                    Entry.Name = Item->DescriptiveNameBase.ToString();
                }
                if (Entry.Name.empty()) Entry.Name = Item->Class->GetName();

                // Helicoprion conversion is asynchronous and scales with item
                // quality. Preserve the values before the first request closes
                // the remote UI and its replicated item objects disappear.
                uint8_t QualityIndex = Item->ItemQualityIndex;
                float ItemRating = Item->ItemRating;
                ReadReflectedByteValue(Item, "ItemQualityIndex", &QualityIndex);
                ReadReflectedFloatValue(Item, "ItemRating", &ItemRating);
                const int32_t QualitySeconds =
                    (std::clamp<int32_t>(QualityIndex, 0, 17) + 1) * 10;
                const int32_t RatingSeconds = std::isfinite(ItemRating) && ItemRating > 0.0f ?
                    static_cast<int32_t>(std::ceil(ItemRating / 5.0f)) : 10;
                const std::string ItemIdentity = NormalizeGachaItemName(
                    Item->Class->GetName() + Entry.Name);
                if (ItemIdentity.find("shovel") != std::string::npos) {
                    // Shovel conversion is fixed at one minute. Two extra
                    // seconds prevent the next RPC from racing the server's
                    // completion/replication boundary.
                    Entry.EstimatedProcessSeconds = 62;
                }
                else {
                    Entry.EstimatedProcessSeconds =
                        std::clamp(std::max(QualitySeconds, RatingSeconds), 10, 180) + 5;
                }
                State.Queue.push_back(std::move(Entry));
                if (!ClientAllowsGrinding) ++State.ClientFilterFallbackItems;
            }

            ++State.ScanAttempts;
            State.SnapshotInventoryFrame = Target->InvUpdatedFrame;

            // An empty list usually means the remote inventory response has not
            // arrived yet. Retry briefly instead of permanently caching it.
            if (ItemCount == 0 && State.ScanAttempts < 5) {
                State.ScanAt = Now + std::chrono::milliseconds(500);
                g_AutoGrindHelicoprionStatus = std::format(
                    "已锁定旋齿鲨，正在同步背包（{}/4）", State.ScanAttempts);
                return;
            }

            State.SnapshotReady = true;
            g_AutoGrindHelicoprionStatus = std::format(
                "已锁定旋齿鲨：排队 {} 件，排除 {} 件，服务器复核 {} 件",
                State.Queue.size(), State.ExcludedItems, State.ClientFilterFallbackItems);
        }

        // If items arrive just after the last empty snapshot while the UI is
        // still open, arm one more scan without requiring the user to close it.
        if (State.Queue.empty() && ViewingInventory == Target &&
            State.SnapshotInventoryFrame != Target->InvUpdatedFrame) {
            State.SnapshotReady = false;
            State.ScanAttempts = 0;
            State.ScanAt = Now + std::chrono::milliseconds(250);
            g_AutoGrindHelicoprionStatus = "检测到背包更新，正在重新扫描";
            return;
        }

        if (State.Queue.empty()) {
            g_AutoGrindHelicoprionStatus = std::format("该旋齿鲨背包没有可提交研磨的物品；排除 {} 件",
                State.ExcludedItems);
            return;
        }

        if (State.AwaitingAcceptance && State.CurrentIndex < State.Queue.size()) {
            QueueEntry& Entry = State.Queue[State.CurrentIndex];

            // A successful Helicoprion request closes the remote inventory.
            // Unlike a local void RPC call, this is an observable server-driven
            // acknowledgement that processing actually started.
            if (PC->GetViewingInventory() != Target) {
                State.AwaitingAcceptance = false;
                State.AcceptanceFailed = false;
                ++State.CurrentIndex;
                State.RetryAt = Now + std::chrono::seconds(Entry.EstimatedProcessSeconds);
                g_AutoGrindHelicoprionStatus = std::format(
                    "服务器已确认开始研磨：{}；进度 {}/{}，预计等待 {} 秒",
                    Entry.Name, State.CurrentIndex, State.Queue.size(), Entry.EstimatedProcessSeconds);
                return;
            }

            if (Now < State.AcceptanceDeadline) {
                const int64_t RemainingMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                    State.AcceptanceDeadline - Now).count();
                const int64_t RemainingSeconds = std::max<int64_t>(
                    (RemainingMilliseconds + 999) / 1000, 0);
                g_AutoGrindHelicoprionStatus = std::format(
                    "研磨请求已发送：{}；等待服务器确认（{} 秒）",
                    Entry.Name, RemainingSeconds);
                return;
            }

            State.AwaitingAcceptance = false;
            if (Entry.RequestAttempts < 2) {
                State.RetryAt = Now + std::chrono::seconds(1);
                g_AutoGrindHelicoprionStatus = std::format(
                    "服务器未确认研磨：{}；1 秒后重试一次",
                    Entry.Name);
                return;
            }

            State.AcceptanceFailed = true;
        }

        if (State.AcceptanceFailed && State.CurrentIndex < State.Queue.size()) {
            const QueueEntry& Entry = State.Queue[State.CurrentIndex];
            g_AutoGrindHelicoprionStatus = std::format(
                "服务器拒绝研磨：{}（已尝试 2 次）；队列已停止，不会误报成功",
                Entry.Name);
            return;
        }

        if (State.CurrentIndex > 0 && Now < State.RetryAt) {
            const QueueEntry& ProcessingEntry = State.Queue[State.CurrentIndex - 1];
            const int64_t RemainingMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                State.RetryAt - Now).count();
            const int64_t RemainingSeconds = std::max<int64_t>(
                (RemainingMilliseconds + 999) / 1000, 0);
            g_AutoGrindHelicoprionStatus = std::format(
                "正在研磨：{}；进度 {}/{}，预计剩余 {} 秒",
                ProcessingEntry.Name, State.CurrentIndex, State.Queue.size(), RemainingSeconds);
            return;
        }

        if (State.CurrentIndex < State.Queue.size()) {
            if (Now < State.RetryAt) return;

            QueueEntry& Entry = State.Queue[State.CurrentIndex];

            // This RPC is reliable. Once the first grind closes the remote UI,
            // the client clears its replicated item list; absence from that list
            // is therefore not proof that every remaining item was processed.
            // Submit the immutable snapshot IDs one at a time instead.
            const bool CanConfirmByInventoryClose = PC->GetViewingInventory() == Target;
            PC->ServerGrindItemInRemoteInventory(Target, Entry.ItemID, false);
            ++Entry.RequestAttempts;

            if (CanConfirmByInventoryClose) {
                State.AwaitingAcceptance = true;
                State.AcceptanceDeadline = Now + std::chrono::seconds(5);
                g_AutoGrindHelicoprionStatus = std::format(
                    "研磨请求已发送：{}；等待服务器确认",
                    Entry.Name);
                return;
            }

            // After the first accepted request closes the UI there is no close
            // event available for later items. They retain the same validated
            // inventory and single-item request shape, and remain paced by the
            // full conversion duration.
            ++State.CurrentIndex;
            State.RetryAt = Now + std::chrono::seconds(Entry.EstimatedProcessSeconds);
            g_AutoGrindHelicoprionStatus = std::format(
                "后台研磨请求已发送：{}；进度 {}/{}，本件等待 {} 秒",
                Entry.Name, State.CurrentIndex, State.Queue.size(), Entry.EstimatedProcessSeconds);
            return;
        }

        g_AutoGrindHelicoprionStatus = std::format("本批次已按研磨周期全部提交：{} 件；扫描时排除 {} 个隐藏/不可研磨条目",
            State.Queue.size(), State.ExcludedItems);
    }

    std::string GetAutoGrindHelicoprionStatus() {
        return g_AutoGrindHelicoprionStatus;
    }

    void ExpandPlayerInventory(SDK::UWorld* World) {
        const int32_t ExpandedInventoryLimit = static_cast<int32_t>(std::lround(
            std::clamp(g_Config::PlayerInventorySlotLimit, 350.0f, 5000.0f)));

        struct InventoryLimitState {
            SDK::UPrimalInventoryComponent* Inventory = nullptr;
            int32_t ObjectIndex = -1;
            int32_t MaxInventoryItems = 0;
            int32_t StartingAbsoluteMaxInventoryItems = 0;
            int32_t AbsoluteMaxInventoryItems = 0;
            bool IgnoreMaxInventoryItems = false;
            bool HasMaxInventoryItems = false;
            bool HasStartingAbsoluteMaxInventoryItems = false;
            bool HasAbsoluteMaxInventoryItems = false;
            bool HasIgnoreMaxInventoryItems = false;
            bool Patched = false;
            int32_t AppliedLimit = 0;
        };
        static InventoryLimitState State;

        auto RestoreState = []() {
            SDK::UPrimalInventoryComponent* Inventory = ResolveCachedObject(
                State.Inventory, State.ObjectIndex);
            if (Inventory && State.Patched) {
                if (State.HasIgnoreMaxInventoryItems) {
                    WriteReflectedBoolValue(Inventory, "bIgnoreMaxInventoryItems",
                        State.IgnoreMaxInventoryItems);
                }
                if (State.HasMaxInventoryItems) {
                    WriteReflectedInt32Value(Inventory, "MaxInventoryItems",
                        State.MaxInventoryItems);
                }
                if (State.HasStartingAbsoluteMaxInventoryItems) {
                    WriteReflectedInt32Value(Inventory, "StartingAbsoluteMaxInventoryItems",
                        State.StartingAbsoluteMaxInventoryItems);
                }
                if (State.HasAbsoluteMaxInventoryItems) {
                    WriteReflectedInt32Value(Inventory, "AbsoluteMaxInventoryItems",
                        State.AbsoluteMaxInventoryItems);
                }
                Inventory->InventoryRefresh();
            }
            State = InventoryLimitState{};
        };

        if (!g_Config::bExpandPlayerInventory) {
            RestoreState();
            return;
        }

        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

        SDK::ULocalPlayer* LP = World->OwningGameInstance->LocalPlayers[0];
        if (!LP || !LP->PlayerController || !LP->PlayerController->IsA(SDK::AShooterPlayerController::StaticClass())) return;

        SDK::AShooterPlayerController* PC = static_cast<SDK::AShooterPlayerController*>(LP->PlayerController);
        SDK::APrimalCharacter* InventoryOwner = GetPlayerInventoryOwner(PC);
        if (!InventoryOwner) return;

        SDK::UPrimalInventoryComponent* Inventory = InventoryOwner->MyInventoryComponent;
        if (!Inventory || !Inventory->Class) return;

        if (State.Inventory != Inventory || State.ObjectIndex != Inventory->Index) {
            RestoreState();
            State.Inventory = Inventory;
            State.ObjectIndex = Inventory->Index;
            State.HasIgnoreMaxInventoryItems = ReadReflectedBoolValue(Inventory,
                "bIgnoreMaxInventoryItems", &State.IgnoreMaxInventoryItems);
            State.HasMaxInventoryItems = ReadReflectedInt32Value(Inventory,
                "MaxInventoryItems", &State.MaxInventoryItems);
            State.HasStartingAbsoluteMaxInventoryItems = ReadReflectedInt32Value(Inventory,
                "StartingAbsoluteMaxInventoryItems", &State.StartingAbsoluteMaxInventoryItems);
            State.HasAbsoluteMaxInventoryItems = ReadReflectedInt32Value(Inventory,
                "AbsoluteMaxInventoryItems", &State.AbsoluteMaxInventoryItems);
        }

        // Ignore the ordinary slot cap and raise only the engine's absolute
        // safety ceiling. Weight, stack sizes and every non-player inventory
        // remain untouched.
        const bool WroteIgnore = WriteReflectedBoolValue(Inventory, "bIgnoreMaxInventoryItems", true);
        const bool WroteMax = WriteReflectedInt32Value(Inventory, "MaxInventoryItems", ExpandedInventoryLimit);
        const bool WroteStartingAbsolute = WriteReflectedInt32Value(Inventory,
            "StartingAbsoluteMaxInventoryItems", ExpandedInventoryLimit);
        const bool WroteAbsolute = WriteReflectedInt32Value(Inventory,
            "AbsoluteMaxInventoryItems", ExpandedInventoryLimit);

        // The character mirrors the result of IsAtMaxInventoryItems for HUD
        // and pickup checks. Clear that replicated/transient latch as well.
        WriteReflectedBoolValue(InventoryOwner, "bIsAtMaxInventoryItems", false);

        if ((!State.Patched || State.AppliedLimit != ExpandedInventoryLimit) &&
            (WroteIgnore || WroteMax || WroteStartingAbsolute || WroteAbsolute)) {
            State.Patched = true;
            State.AppliedLimit = ExpandedInventoryLimit;
            Inventory->InventoryRefresh();

            const int32_t EffectiveMax = Inventory->GetMaxInventoryItems(true);
            const int32_t EffectiveAbsolute = Inventory->GetAbsoluteMaxInventoryItems(false);
            g_LogManager::AddLog(120, 255, 180, 255,
                std::format("背包上限已应用: 普通 {} / 绝对 {}", EffectiveMax, EffectiveAbsolute));
        }
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

    std::vector<SkillCooldownView> GetLostColonyCooldowns(SDK::UWorld* World) {
        std::vector<SkillCooldownView> Result;
        SDK::AShooterCharacter* Human = GetLocalHumanCharacter(World);
        if (!Human || !IsUsableObject(Human)) return Result;

        ReflectedStructArrayView Cooldowns;
        if (!GetReflectedStructArray(Human, "SkillCooldowns", 512, &Cooldowns) ||
            !Cooldowns.ElementStruct) {
            return Result;
        }

        SDK::FProperty* SkillNameProperty = FindReflectedProperty(Cooldowns.ElementStruct, "SkillName");
        SDK::FProperty* CooldownDurationProperty =
            FindReflectedProperty(Cooldowns.ElementStruct, "CooldownDuration");
        if (!SkillNameProperty || !SkillNameProperty->ClassPrivate || SkillNameProperty->Offset < 0 ||
            SkillNameProperty->Offset + static_cast<int32_t>(sizeof(SDK::FName)) > Cooldowns.ElementSize ||
            !EqualsNameIgnoreCase(SkillNameProperty->ClassPrivate->Name.ToString(), "NameProperty") ||
            !CooldownDurationProperty || !CooldownDurationProperty->ClassPrivate ||
            CooldownDurationProperty->Offset < 0 ||
            CooldownDurationProperty->Offset + static_cast<int32_t>(sizeof(float)) > Cooldowns.ElementSize ||
            !EqualsNameIgnoreCase(CooldownDurationProperty->ClassPrivate->Name.ToString(), "FloatProperty")) {
            return Result;
        }

        for (int32_t Index = 0; Index < Cooldowns.Num; ++Index) {
            const uint8_t* Element = Cooldowns.Data + static_cast<size_t>(Index) * Cooldowns.ElementSize;
            const SDK::FName SkillNameValue =
                *reinterpret_cast<const SDK::FName*>(Element + SkillNameProperty->Offset);
            const float Duration =
                *reinterpret_cast<const float*>(Element + CooldownDurationProperty->Offset);
            const std::string SkillName = SkillNameValue.ToString();

            const float Remaining = Human->GetSkillCooldownTimeRemaining(SkillNameValue);
            if (!std::isfinite(Remaining) || Remaining <= 0.01f) continue;

            Result.push_back({
                SkillName,
                Remaining,
                std::isfinite(Duration) ? Duration : 0.0f,
                false
                });
        }

        std::sort(Result.begin(), Result.end(), [](const SkillCooldownView& Left, const SkillCooldownView& Right) {
            return Left.RemainingSeconds > Right.RemainingSeconds;
            });
        return Result;
    }

    namespace {
        struct AutoHarvestSkippedTarget {
            SDK::FVector Location{};
            std::chrono::steady_clock::time_point Until{};
        };

        struct AutoHarvestRuntimeState {
            bool WasEnabled = false;
            bool TekStriderElementShardMode = false;
            bool TekStriderBeamActive = false;
            bool OwnsForceTurn = false;
            bool HasTarget = false;
            bool AttackHeld = false;
            bool OwnsSprint = false;
            SDK::APrimalDinoCharacter* SprintDino = nullptr;
            int32_t SprintDinoObjectIndex = -1;
            SDK::FVector TargetLocation{};
            SDK::FVector AvoidDirection{};
            SDK::FVector MovementSampleLocation{};
            std::string TargetType;
            std::vector<SDK::FVector> NavigationPoints;
            std::vector<AutoHarvestSkippedTarget> SkippedTargets;
            size_t NavigationPointIndex = 0;
            bool NavigationPathAvailable = false;
            bool NavigationPathPartial = false;
            std::chrono::steady_clock::time_point NextUpdate{};
            std::chrono::steady_clock::time_point NextAttack{};
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
        };

        AutoHarvestRuntimeState s_autoHarvestState{};

        struct AutoHarvestAquaticPitchState {
            SDK::APrimalDinoCharacter* Dino = nullptr;
            int32_t DinoObjectIndex = -1;
            SDK::UPrimalCharacterMovement* Movement = nullptr;
            int32_t MovementObjectIndex = -1;
            bool UseControllerRotationPitch = false;
            bool UseControllerRotationYaw = false;
            bool ForceAccelerationFollowsRotationInSwimming = false;
            bool IgnoreRotationAccelerationWhenSwimming = false;
            bool ZeroPitchWhenNoAcceleration = false;
        };

        AutoHarvestAquaticPitchState s_autoHarvestAquaticPitchState{};
        std::mutex s_autoHarvestStatusMutex;
        std::string s_autoHarvestStatus = "未启用";

        void SetAutoHarvestStatus(std::string Status) {
            std::scoped_lock Lock(s_autoHarvestStatusMutex);
            s_autoHarvestStatus = std::move(Status);
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

        bool StopAutoHarvestTekStriderBeam() {
            if (!s_autoHarvestState.TekStriderBeamActive) return true;
            if (!SendAutoHarvestTekStriderRigToggle()) return false;
            s_autoHarvestState.TekStriderBeamActive = false;
            return true;
        }

        void ReleaseAutoHarvestSprint() {
            if (s_autoHarvestState.OwnsSprint &&
                s_autoHarvestState.SprintDino &&
                s_autoHarvestState.SprintDinoObjectIndex >= 0 &&
                IsUsableObject(s_autoHarvestState.SprintDino) &&
                s_autoHarvestState.SprintDino->Index ==
                    s_autoHarvestState.SprintDinoObjectIndex) {
                s_autoHarvestState.SprintDino->OnStopRunning();
            }
            s_autoHarvestState.OwnsSprint = false;
            s_autoHarvestState.SprintDino = nullptr;
            s_autoHarvestState.SprintDinoObjectIndex = -1;
            s_autoHarvestState.NextSprintRequest = {};
        }

        void UpdateAutoHarvestSprint(SDK::APrimalDinoCharacter* Dino,
            bool ShouldSprint, std::chrono::steady_clock::time_point Now) {
            if (!ShouldSprint || !Dino || !IsUsableObject(Dino)) {
                ReleaseAutoHarvestSprint();
                return;
            }

            if (s_autoHarvestState.SprintDino != Dino ||
                s_autoHarvestState.SprintDinoObjectIndex != Dino->Index) {
                ReleaseAutoHarvestSprint();
                s_autoHarvestState.SprintDino = Dino;
                s_autoHarvestState.SprintDinoObjectIndex = Dino->Index;
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

        void RestoreAutoHarvestAquaticPitchControl() {
            AutoHarvestAquaticPitchState& State = s_autoHarvestAquaticPitchState;
            if (State.Dino && State.DinoObjectIndex >= 0 &&
                IsUsableObject(State.Dino) && State.Dino->Index == State.DinoObjectIndex) {
                State.Dino->bUseControllerRotationPitch = State.UseControllerRotationPitch;
                State.Dino->bUseControllerRotationYaw = State.UseControllerRotationYaw;
            }
            if (State.Movement && State.MovementObjectIndex >= 0 &&
                IsUsableObject(State.Movement) &&
                State.Movement->Index == State.MovementObjectIndex) {
                State.Movement->bForceAccelerationFollowsRotationInSwimming =
                    State.ForceAccelerationFollowsRotationInSwimming;
                State.Movement->bIgnoreRotationAccelerationWhenSwimming =
                    State.IgnoreRotationAccelerationWhenSwimming;
                State.Movement->bZeroPitchWhenNoAcceleration =
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
            AutoHarvestAquaticPitchState& State = s_autoHarvestAquaticPitchState;
            if (State.Dino != Dino || State.DinoObjectIndex != Dino->Index ||
                State.Movement != Movement || State.MovementObjectIndex != Movement->Index) {
                RestoreAutoHarvestAquaticPitchControl();
                State.Dino = Dino;
                State.DinoObjectIndex = Dino->Index;
                State.Movement = Movement;
                State.MovementObjectIndex = Movement->Index;
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

        SDK::TArray<SDK::AActor*> MakeAutoHarvestTraceIgnoreActors(
            SDK::APrimalDinoCharacter* Dino) {
            SDK::TArray<SDK::AActor*> IgnoreActors;
            if (Dino) {
                IgnoreActors.Add(Dino);
                if (SDK::AShooterCharacter* Rider = Dino->Rider.Get()) IgnoreActors.Add(Rider);
            }
            return IgnoreActors;
        }

        bool RefreshAutoHarvestNavigationPath(SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino, const SDK::FVector& Start,
            const SDK::FVector& End) {
            s_autoHarvestState.NavigationPoints.clear();
            s_autoHarvestState.NavigationPointIndex = 0;
            s_autoHarvestState.NavigationPathAvailable = false;
            s_autoHarvestState.NavigationPathPartial = false;

            if (!World || !Dino) return false;
            SDK::UNavigationSystemV1* NavigationSystem =
                SDK::UNavigationSystemV1::GetNavigationSystem(World);
            if (!IsUsableObject(NavigationSystem)) return false;

            const int32_t NavigationDataCount = NavigationSystem->NavDataSet.Num();
            const bool HasNavigationData = IsUsableObject(NavigationSystem->MainNavData) ||
                (NavigationDataCount > 0 && NavigationDataCount <= 128);
            if (!HasNavigationData) return false;

            struct NavigationCandidate {
                std::vector<SDK::FVector> Points;
                double Score = std::numeric_limits<double>::infinity();
                bool Partial = true;
            };
            NavigationCandidate Best{};

            auto EvaluateCandidate = [&](const SDK::FVector& CandidateEnd) {
                if (!IsFiniteAutoHarvestVector(CandidateEnd)) return;
                SDK::UNavigationPath* Path =
                    SDK::UNavigationSystemV1::FindPathToLocationSynchronously(
                        World, Start, CandidateEnd, Dino,
                        SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr));
                if (!IsUsableObject(Path) || !Path->IsValid()) return;
                const int32_t PointCount = Path->PathPoints.Num();
                if (PointCount < 2 || PointCount > 512) return;

                std::vector<SDK::FVector> Points;
                Points.reserve(PointCount);
                for (int32_t Index = 0; Index < PointCount; ++Index) {
                    const SDK::FVector Point = Path->PathPoints[Index];
                    if (IsFiniteAutoHarvestVector(Point)) Points.push_back(Point);
                }
                if (Points.size() < 2) return;

                double PathLength = 0.0;
                for (size_t Index = 1; Index < Points.size(); ++Index) {
                    const double SegmentSquared = AutoHarvestDistanceSquared(
                        Points[Index - 1], Points[Index]);
                    if (!std::isfinite(SegmentSquared)) return;
                    PathLength += std::sqrt(std::max(0.0, SegmentSquared));
                }
                const double EndDistanceSquared = AutoHarvestDistanceSquared(
                    Points.back(), End);
                if (!std::isfinite(EndDistanceSquared)) return;

                const bool Partial = Path->IsPartial();
                // A complete route is always preferred. Among routes with the
                // same completeness, favor the shorter walk while retaining a
                // small preference for an endpoint close to attack range.
                const double Score = PathLength + std::sqrt(EndDistanceSquared) * 0.25 +
                    (Partial ? 2000000.0 : 0.0);
                if (Score >= Best.Score) return;
                Best.Points = std::move(Points);
                Best.Score = Score;
                Best.Partial = Partial;
            };

            EvaluateCandidate(End);

            if (g_Config::bAutoHarvestExperimentalNavigation) {
                float CapsuleRadius = 75.0f;
                float CapsuleHalfHeight = 100.0f;
                if (Dino->CapsuleComponent && IsUsableObject(Dino->CapsuleComponent)) {
                    CapsuleRadius = std::max(25.0f,
                        Dino->CapsuleComponent->GetScaledCapsuleRadius());
                    CapsuleHalfHeight = std::max(50.0f,
                        Dino->CapsuleComponent->GetScaledCapsuleHalfHeight());
                }

                SDK::FVector TowardStart{};
                if (MakeHorizontalAutoHarvestDirection(End, Start, &TowardStart)) {
                    const double ApproachRadiusUU = std::clamp(
                        std::max(static_cast<double>(g_Config::AutoHarvestAttackDistanceMeters) *
                            80.0, static_cast<double>(CapsuleRadius) * 1.2),
                        250.0, 1000.0);
                    const SDK::FVector QueryExtent{
                        std::max(200.0, static_cast<double>(CapsuleRadius) * 1.75),
                        std::max(200.0, static_cast<double>(CapsuleRadius) * 1.75),
                        std::max(300.0, static_cast<double>(CapsuleHalfHeight) * 1.5)
                    };
                    constexpr std::array<float, 5> CandidateAngles{
                        0.0f, -50.0f, 50.0f, -105.0f, 105.0f
                    };
                    for (const float Angle : CandidateAngles) {
                        const SDK::FVector OffsetDirection =
                            RotateAutoHarvestDirection(TowardStart, Angle);
                        const SDK::FVector Candidate{
                            End.X + OffsetDirection.X * ApproachRadiusUU,
                            End.Y + OffsetDirection.Y * ApproachRadiusUU,
                            End.Z
                        };
                        SDK::FVector Projected{};
                        if (!SDK::UNavigationSystemV1::K2_ProjectPointToNavigation(
                            World, Candidate, &Projected, NavigationSystem->MainNavData,
                            SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr),
                            QueryExtent)) {
                            continue;
                        }
                        const double MaximumVerticalShift = std::max(
                            350.0, static_cast<double>(CapsuleHalfHeight) * 1.5);
                        if (!IsFiniteAutoHarvestVector(Projected) ||
                            std::abs(Projected.Z - End.Z) > MaximumVerticalShift) {
                            continue;
                        }
                        EvaluateCandidate(Projected);
                    }
                }
            }

            if (Best.Points.size() < 2) return false;
            s_autoHarvestState.NavigationPoints = std::move(Best.Points);
            s_autoHarvestState.NavigationPointIndex = 1;
            s_autoHarvestState.NavigationPathAvailable = true;
            s_autoHarvestState.NavigationPathPartial = Best.Partial;
            return true;
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
                if (g_Config::bAutoHarvestExperimentalNavigation && World && Dino) {
                    // Navigation raycasts cheaply determine whether intermediate
                    // corners can be skipped. Looking only a few points ahead
                    // smooths zig-zags without cutting across non-navigable space.
                    const size_t First = s_autoHarvestState.NavigationPointIndex;
                    const size_t Last = std::min(
                        s_autoHarvestState.NavigationPoints.size() - 1, First + 4);
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

            double RemainingDistance = 0.0;
            SDK::FVector Previous = CurrentLocation;
            for (size_t Index = s_autoHarvestState.NavigationPointIndex;
                Index < s_autoHarvestState.NavigationPoints.size(); ++Index) {
                const double SegmentDistance = Distance(
                    Previous, s_autoHarvestState.NavigationPoints[Index]);
                if (!std::isfinite(SegmentDistance)) {
                    return std::numeric_limits<double>::infinity();
                }
                RemainingDistance += SegmentDistance;
                Previous = s_autoHarvestState.NavigationPoints[Index];
            }

            const double FinalSegmentDistance = Distance(Previous, FinalTarget);
            return std::isfinite(FinalSegmentDistance)
                ? RemainingDistance + FinalSegmentDistance
                : std::numeric_limits<double>::infinity();
        }

        bool AutoHarvestHasSafeGround(SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino, const SDK::FVector& CurrentLocation,
            const SDK::FVector& Direction, float CapsuleRadius, float CapsuleHalfHeight,
            float MaxSafeDropMeters) {
            if (!World || !Dino) return false;
            if (Dino->CanFly() && Dino->bIsFlying) return true;

            const double ProbeDistance = std::clamp(
                static_cast<double>(CapsuleRadius) * 1.4 + 125.0, 250.0, 900.0);
            SDK::FVector TraceStart{
                CurrentLocation.X + Direction.X * ProbeDistance,
                CurrentLocation.Y + Direction.Y * ProbeDistance,
                CurrentLocation.Z + std::max(100.0, static_cast<double>(CapsuleHalfHeight) * 0.25)
            };
            SDK::FVector TraceEnd = TraceStart;
            TraceEnd.Z = CurrentLocation.Z - static_cast<double>(CapsuleHalfHeight) -
                std::clamp(static_cast<double>(MaxSafeDropMeters), 0.5, 8.0) * 100.0;

            SDK::FHitResult Hit{};
            const SDK::TArray<SDK::AActor*> IgnoreActors =
                MakeAutoHarvestTraceIgnoreActors(Dino);
            const bool HasGround = SDK::UKismetSystemLibrary::LineTraceSingle(
                World, TraceStart, TraceEnd, SDK::ETraceTypeQuery::TraceTypeQuery1,
                false, IgnoreActors, SDK::EDrawDebugTrace::None, &Hit, true,
                SDK::FLinearColor(0, 0, 0, 0), SDK::FLinearColor(0, 0, 0, 0), 0.0f);
            return HasGround && Hit.bBlockingHit && Hit.ImpactNormal.Z >= 0.22;
        }

        bool AutoHarvestDirectionIsBlocked(SDK::UWorld* World,
            SDK::APrimalDinoCharacter* Dino, const SDK::FVector& CurrentLocation,
            const SDK::FVector& Direction, float CapsuleRadius,
            const SDK::FVector& FinalTarget, float AttackDistanceMeters,
            SDK::FHitResult* OutBlockingHit = nullptr) {
            if (OutBlockingHit) *OutBlockingHit = {};
            if (!World || !Dino) return true;
            const double ProbeDistance = std::clamp(
                static_cast<double>(CapsuleRadius) * 2.0 + 150.0, 300.0, 1100.0);
            const float ProbeRadius = static_cast<float>(std::clamp(
                static_cast<double>(CapsuleRadius) * 0.38, 35.0, 200.0));
            SDK::FVector TraceEnd{
                CurrentLocation.X + Direction.X * ProbeDistance,
                CurrentLocation.Y + Direction.Y * ProbeDistance,
                CurrentLocation.Z
            };

            SDK::FHitResult Hit{};
            const SDK::TArray<SDK::AActor*> IgnoreActors =
                MakeAutoHarvestTraceIgnoreActors(Dino);
            const bool HasHit = SDK::UKismetSystemLibrary::SphereTraceSingle(
                World, CurrentLocation, TraceEnd, ProbeRadius,
                SDK::ETraceTypeQuery::TraceTypeQuery1, false, IgnoreActors,
                SDK::EDrawDebugTrace::None, &Hit, true,
                SDK::FLinearColor(0, 0, 0, 0), SDK::FLinearColor(0, 0, 0, 0), 0.0f);
            if (!HasHit || !Hit.bBlockingHit) return false;
            if (Hit.bStartPenetrating) {
                if (OutBlockingHit) *OutBlockingHit = Hit;
                return true;
            }
            if (Hit.ImpactNormal.Z >= 0.55) return false;

            // Only ignore a hit very close to the selected node itself. The old
            // attack-distance-sized allowance also ignored cliffs and walls in
            // front of a node, so the mount continuously walked into them.
            const double AllowedTargetDistanceUU = std::clamp(
                static_cast<double>(CapsuleRadius) * 0.75 + 150.0, 250.0, 500.0);
            const bool Blocked = AutoHarvestDistanceSquared(Hit.ImpactPoint, FinalTarget) >
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
            const SDK::FVector& CurrentLocation, const SDK::FVector& BaseDirection,
            float CapsuleRadius, float CapsuleHalfHeight, const SDK::FVector& FinalTarget,
            float AttackDistanceMeters, float MaxSafeDropMeters,
            const std::chrono::steady_clock::time_point Now,
            bool ForceDetour = false, bool PreferRight = false) {
            AutoHarvestSafeDirectionResult Result{};

            auto IsDirectionSafe = [&](const SDK::FVector& Direction, bool* OutCliff) {
                const bool HasGround = AutoHarvestHasSafeGround(World, Dino, CurrentLocation,
                    Direction, CapsuleRadius, CapsuleHalfHeight, MaxSafeDropMeters);
                if (!HasGround) {
                    if (OutCliff) *OutCliff = true;
                    return false;
                }
                return !AutoHarvestDirectionIsBlocked(World, Dino, CurrentLocation,
                    Direction, CapsuleRadius, FinalTarget, AttackDistanceMeters);
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
                Result.CliffDetected = Cliff;
                s_autoHarvestState.AvoidUntil = {};
            }

            const std::array<float, 7> SteeringAngles = ForceDetour
                ? (PreferRight
                    ? std::array<float, 7>{ 72.0f, -72.0f, 105.0f, -105.0f, 140.0f, -140.0f, 180.0f }
                    : std::array<float, 7>{ -72.0f, 72.0f, -105.0f, 105.0f, -140.0f, 140.0f, 180.0f })
                : std::array<float, 7>{ 0.0f, -32.0f, 32.0f, -62.0f, 62.0f, -100.0f, 100.0f };
            for (const float Angle : SteeringAngles) {
                const SDK::FVector Candidate = RotateAutoHarvestDirection(BaseDirection, Angle);
                bool Cliff = false;
                if (!IsDirectionSafe(Candidate, &Cliff)) {
                    Result.CliffDetected = Result.CliffDetected || Cliff;
                    continue;
                }
                Result.Found = true;
                Result.Avoiding = std::abs(Angle) > 0.1f;
                Result.Direction = Candidate;
                if (Result.Avoiding) {
                    s_autoHarvestState.AvoidDirection = Candidate;
                    s_autoHarvestState.AvoidUntil = Now + std::chrono::milliseconds(
                        ForceDetour ? 1600 : 700);
                }
                return Result;
            }
            return Result;
        }

        void ClearAutoHarvestTarget() {
            ReleaseAutoHarvestAttack();
            StopAutoHarvestTekStriderBeam();
            ReleaseAutoHarvestSprint();
            s_autoHarvestState.HasTarget = false;
            s_autoHarvestState.TargetLocation = {};
            s_autoHarvestState.TargetType.clear();
            s_autoHarvestState.TargetLastSeen = {};
            s_autoHarvestState.TargetAcquired = {};
            s_autoHarvestState.AttackStarted = {};
            s_autoHarvestState.NavigationPoints.clear();
            s_autoHarvestState.NavigationPointIndex = 0;
            s_autoHarvestState.NavigationPathAvailable = false;
            s_autoHarvestState.NavigationPathPartial = false;
            s_autoHarvestState.NextPathRefresh = {};
            s_autoHarvestState.AvoidDirection = {};
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
            s_autoHarvestState.BestApproachDistanceUU =
                std::numeric_limits<double>::infinity();
            s_autoHarvestState.ApproachProgressUsesNavigation = false;
        }
    }

    std::string GetAutoHarvestStatus() {
        std::scoped_lock Lock(s_autoHarvestStatusMutex);
        return s_autoHarvestStatus;
    }

    void AutoHarvestResources(SDK::UWorld* World) {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point Now = Clock::now();
        const bool TekStriderElementShardMode =
            g_Config::bAutoHarvestTekStriderElementShard;

        if (!g_Config::bAutoHarvestResources) {
            if (s_autoHarvestState.AttackHeld) ReleaseAutoHarvestAttack();
            if (!StopAutoHarvestTekStriderBeam()) {
                SetAutoHarvestStatus("自动采集已关闭；返回游戏并关闭菜单后将停止跨步者采集钻头");
                return;
            }
            ReleaseAutoHarvestSprint();
            if (s_autoHarvestState.OwnsForceTurn) g_Config::bForceTurn = false;
            RestoreAutoHarvestAquaticPitchControl();
            if (s_autoHarvestState.WasEnabled) SetAutoHarvestStatus("未启用");
            s_autoHarvestState = AutoHarvestRuntimeState{};
            return;
        }

        if (!s_autoHarvestState.WasEnabled) {
            s_autoHarvestState.WasEnabled = true;
            s_autoHarvestState.TekStriderElementShardMode =
                TekStriderElementShardMode;
            s_autoHarvestState.NextUpdate = Clock::time_point::min();
            s_autoHarvestState.NextAttack = Now + std::chrono::milliseconds(250);
        }
        else if (s_autoHarvestState.TekStriderElementShardMode !=
            TekStriderElementShardMode) {
            if (!StopAutoHarvestTekStriderBeam()) {
                SetAutoHarvestStatus("返回游戏并关闭菜单后才能切换跨步者碎片矿模式");
                return;
            }
            ClearAutoHarvestTarget();
            s_autoHarvestState.TekStriderElementShardMode =
                TekStriderElementShardMode;
        }
        if (!g_Config::bForceTurn) {
            g_Config::bForceTurn = true;
            s_autoHarvestState.OwnsForceTurn = true;
        }

        // The actor tick hook runs for many actors. This cap makes the automation
        // execute at most once every 30 ms while still releasing a held click promptly.
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
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            SetAutoHarvestStatus(s_autoHarvestState.TekStriderBeamActive
                ? "菜单打开，已暂停；关闭菜单后将安全停止跨步者采集钻头"
                : "菜单打开，已暂停");
            return;
        }
        const HWND GameWindow = g_MDX12::g_ProcessWindow::g_mainWindow;
        if (!GameWindow || GetForegroundWindow() != GameWindow) {
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            SetAutoHarvestStatus(s_autoHarvestState.TekStriderBeamActive
                ? "游戏未处于前台，已暂停；返回游戏后将安全停止跨步者采集钻头"
                : "游戏未处于前台，已暂停");
            return;
        }

        if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() <= 0) {
            ReleaseAutoHarvestAttack();
            RestoreAutoHarvestAquaticPitchControl();
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("等待进入世界");
            return;
        }

        SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
        if (!IsUsableObject(LocalPC) || !LocalPC->Pawn ||
            !LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            ReleaseAutoHarvestAttack();
            RestoreAutoHarvestAquaticPitchControl();
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("需要骑乘可采集资源的生物");
            return;
        }
        if (LocalPC->bShowMouseCursor) {
            ReleaseAutoHarvestAttack();
            ReleaseAutoHarvestSprint();
            RestoreAutoHarvestAquaticPitchControl();
            SetAutoHarvestStatus("游戏界面或背包已打开，自动采集暂停");
            return;
        }

        SDK::APrimalDinoCharacter* Dino =
            static_cast<SDK::APrimalDinoCharacter*>(LocalPC->Pawn);
        if (!IsUsableObject(Dino) || Dino->IsDead() || !Dino->Rider.Get() || !Dino->IsLocallyControlled()) {
            ReleaseAutoHarvestAttack();
            RestoreAutoHarvestAquaticPitchControl();
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("需要骑乘并控制存活的生物");
            return;
        }

        if (TekStriderElementShardMode && !IsTekStrider(Dino)) {
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("元素碎片模式需要骑乘跨步者");
            return;
        }
        if (TekStriderElementShardMode && !HasTekStriderExcavationRig(Dino)) {
            ClearAutoHarvestTarget();
            SetAutoHarvestStatus("当前跨步者没有装备采集钻头头部配件");
            return;
        }

        const SDK::FVector DinoLocation = Dino->K2_GetActorLocation();
        const float SearchRadius = std::clamp(g_Config::AutoHarvestSearchRadiusMeters, 5.0f, 100.0f);
        const float AttackDistance = TekStriderElementShardMode
            ? std::clamp(g_Config::AutoHarvestTekStriderBeamRangeMeters, 10.0f, 80.0f)
            : std::clamp(g_Config::AutoHarvestAttackDistanceMeters, 1.5f, 15.0f);
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

        if (s_autoHarvestState.HasTarget) {
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
                &ResolvedLocation, &ResolvedType, TekStriderElementShardMode)) {
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
            const bool ApproachTimedOut = s_autoHarvestState.TargetAcquired != Clock::time_point{} &&
                Now - s_autoHarvestState.TargetAcquired > std::chrono::seconds(
                    IsAquaticCreature ? 45 : 30);
            const bool AttackTimedOut = s_autoHarvestState.AttackStarted != Clock::time_point{} &&
                Now - s_autoHarvestState.AttackStarted > std::chrono::seconds(
                    IsAquaticCreature ? 15 : (TekStriderElementShardMode ? 20 : 10));
            if (ApproachTimedOut || AttackTimedOut) {
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
            if (IsAquaticCreature && g_Config::bAutoHarvestApproach &&
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

            // While the mount still has momentum, inspect several nearest
            // candidates but commit only to one in the current forward
            // hemisphere. Rejected rear targets are temporary exclusions for
            // this selection pass; once speed is low they become valid again.
            for (int Attempt = 0; Attempt < (RequireForwardTarget ? 10 : 1); ++Attempt) {
                const auto* ExcludedLocationList = ExcludedLocations.empty()
                    ? nullptr : &ExcludedLocations;
                SDK::FVector CandidateLocation{};
                float CandidateDistance = 0.0f;
                std::string CandidateType;
                if (!g_DrawESP::FindNearestResourceTarget(DinoLocation, SearchRadius,
                    &CandidateLocation, &CandidateDistance, &CandidateType,
                    ExcludedLocationList, 3.0f, TekStriderElementShardMode)) {
                    break;
                }

                if (RequireForwardTarget) {
                    SDK::FVector CandidateDirection{};
                    const bool HasDirection = MakeThreeDimensionalAutoHarvestDirection(
                        DinoLocation, CandidateLocation, &CandidateDirection);
                    const double ForwardDot = HasDirection
                        ? CandidateDirection.X * AquaticForward.X +
                            CandidateDirection.Y * AquaticForward.Y +
                            CandidateDirection.Z * AquaticForward.Z
                        : -1.0;
                    if (ForwardDot < 0.10) {
                        ExcludedLocations.push_back(CandidateLocation);
                        continue;
                    }
                }

                TargetLocation = CandidateLocation;
                TargetDistance = CandidateDistance;
                TargetType = std::move(CandidateType);
                FoundTarget = true;
                break;
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
                ReleaseAutoHarvestAttack();
                StopAutoHarvestTekStriderBeam();
                SetAutoHarvestStatus(ExcludedLocationList
                    ? "正在跳过无法到达的资源并寻找下一个"
                    : (TekStriderElementShardMode
                        ? "范围内没有识别到元素碎片矿石"
                        : "范围内没有已勾选的资源"));
                return;
            }

            s_autoHarvestState.HasTarget = true;
            s_autoHarvestState.TargetLocation = TargetLocation;
            s_autoHarvestState.TargetType = std::move(TargetType);
            s_autoHarvestState.TargetLastSeen = Now;
            s_autoHarvestState.TargetAcquired = Now;
            s_autoHarvestState.AttackStarted = {};
            s_autoHarvestState.NextAttack = Now + std::chrono::milliseconds(250);
            s_autoHarvestState.NextPathRefresh = Clock::time_point::min();
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

        float CapsuleRadius = 75.0f;
        float CapsuleHalfHeight = 100.0f;
        if (Dino->CapsuleComponent && IsUsableObject(Dino->CapsuleComponent)) {
            const float ResolvedRadius = Dino->CapsuleComponent->GetScaledCapsuleRadius();
            const float ResolvedHalfHeight = Dino->CapsuleComponent->GetScaledCapsuleHalfHeight();
            if (std::isfinite(ResolvedRadius) && ResolvedRadius > 1.0f) {
                CapsuleRadius = ResolvedRadius;
            }
            if (std::isfinite(ResolvedHalfHeight) && ResolvedHalfHeight > 1.0f) {
                CapsuleHalfHeight = ResolvedHalfHeight;
            }
        }
        const bool IsGroundCreature = !IsAerialCreature && !IsAquaticCreature;
        const bool UseThreeDimensionalMovement = IsAerialCreature || IsAquaticCreature;

        if (IsGroundCreature) {
            const double DeltaX = s_autoHarvestState.TargetLocation.X - DinoLocation.X;
            const double DeltaY = s_autoHarvestState.TargetLocation.Y - DinoLocation.Y;
            const double HorizontalDistanceUU = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
            const double VerticalDistanceUU = std::abs(
                s_autoHarvestState.TargetLocation.Z - DinoLocation.Z);
            const double NearbyHorizontalLimitUU = std::max(
                static_cast<double>(AttackDistance) * 100.0 + CapsuleRadius * 3.0, 1200.0);
            const double ReachableVerticalUU = std::max(
                static_cast<double>(AttackDistance) * 100.0 * 0.62,
                static_cast<double>(CapsuleHalfHeight) * 0.85 + 75.0);
            if (HorizontalDistanceUU <= NearbyHorizontalLimitUU &&
                VerticalDistanceUU > ReachableVerticalUU) {
                // Do not reject a resource merely because it is currently uphill:
                // a complete nav path whose endpoint reaches the resource layer is
                // valid. A path ending below an overhead shelf is not.
                bool HasReachableLayerPath = false;
                if (g_Config::bAutoHarvestSafeNavigation &&
                    Now >= s_autoHarvestState.NextPathRefresh) {
                    RefreshAutoHarvestNavigationPath(World, Dino, DinoLocation,
                        s_autoHarvestState.TargetLocation);
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

        if (DistanceMeters > AttackDistance && g_Config::bAutoHarvestApproach) {
            SDK::FVector MovementGoal = s_autoHarvestState.TargetLocation;
            const bool UseSafeNavigation = IsGroundCreature &&
                (g_Config::bAutoHarvestSafeNavigation ||
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
                        s_autoHarvestState.NextRecoveryAttempt =
                            Now + std::chrono::milliseconds(1200);
                    }
                }
            }
            if (UseSafeNavigation) {
                if (Now >= s_autoHarvestState.NextPathRefresh) {
                    RefreshAutoHarvestNavigationPath(World, Dino, DinoLocation,
                        s_autoHarvestState.TargetLocation);
                    s_autoHarvestState.NextPathRefresh =
                        Now + std::chrono::milliseconds(
                            g_Config::bAutoHarvestExperimentalNavigation ? 850 : 1400);
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
                        AutoHarvestDirectionIsBlocked(World, Dino, DinoLocation,
                            BaseDirection, CapsuleRadius,
                            s_autoHarvestState.TargetLocation, AttackDistance,
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
                        FindAutoHarvestSafeDirection(World, Dino, DinoLocation, BaseDirection,
                            CapsuleRadius, CapsuleHalfHeight, s_autoHarvestState.TargetLocation,
                            AttackDistance, g_Config::AutoHarvestMaxSafeDropMeters, Now,
                            ForceDetour, (s_autoHarvestState.RecoveryAttempts % 2) == 0);
                    if (!SafeDirection.Found) {
                        ReleaseAutoHarvestAttack();
                        ReleaseAutoHarvestSprint();
                        if (s_autoHarvestState.UnsafeSince == Clock::time_point{}) {
                            s_autoHarvestState.UnsafeSince = Now;
                        }
                        s_autoHarvestState.NextPathRefresh =
                            Now + std::chrono::milliseconds(
                                g_Config::bAutoHarvestExperimentalNavigation ? 220 : 350);

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
        if (IsAquaticCreature && g_Config::bAutoHarvestApproach) {
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
            g_Config::bAutoHarvestSprint && g_Config::bAutoHarvestApproach &&
                IsGroundCreature && DistanceMeters > AttackDistance &&
                HasMovementDirection && !ClearingObstacle,
            Now);

        // Lock only yaw. During approach face the current safe waypoint/steering
        // direction; once in range face the resource itself for the attack.
        SDK::FVector HorizontalTarget = s_autoHarvestState.TargetLocation;
        if (HasMovementDirection && MovementDirectionControlsFacing) {
            HorizontalTarget.X = DinoLocation.X + MovementDirection.X * 1000.0;
            HorizontalTarget.Y = DinoLocation.Y + MovementDirection.Y * 1000.0;
            if (UseThreeDimensionalMovement) {
                HorizontalTarget.Z = DinoLocation.Z + MovementDirection.Z * 1000.0;
            }
        }
        if (!UseThreeDimensionalMovement && !TekStriderElementShardMode) {
            HorizontalTarget.Z = DinoLocation.Z;
        }
        SDK::FRotator TargetRotation = SDK::UKismetMathLibrary::FindLookAtRotation(
            DinoLocation, HorizontalTarget);
        if (!UseThreeDimensionalMovement && !TekStriderElementShardMode) {
            TargetRotation.Pitch = 0.0;
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
            SDK::FRotator DinoRotation = TargetRotation;
            if (!UseThreeDimensionalMovement) DinoRotation.Pitch = 0.0;
            Dino->CharacterMovement->UpdatedComponent->K2_SetWorldRotation(
                DinoRotation, false, nullptr, false);
            Dino->CharacterMovement->UpdatedComponent->RelativeRotation = DinoRotation;
            Dino->CharacterMovement->Velocity = SavedVelocity;
        }
        const SDK::FRotator AppliedDinoRotation = Dino->K2_GetActorRotation();

        bool AquaticMovementApplied = false;
        if (IsAquaticCreature && g_Config::bAutoHarvestApproach &&
            HasMovementDirection) {
            Dino->AddMovementInput(MovementDirection, MovementInputScale, true);
            AquaticMovementApplied = true;
        }

        if (DistanceMeters > AttackDistance) {
            if (TekStriderElementShardMode) StopAutoHarvestTekStriderBeam();
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
            if (g_Config::bAutoHarvestApproach) {
                if (HasMovementDirection && !AquaticMovementApplied) {
                    Dino->AddMovementInput(MovementDirection, MovementInputScale, true);
                }
                if (AquaticBraking) {
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

        if (s_autoHarvestState.AttackStarted == Clock::time_point{}) {
            s_autoHarvestState.AttackStarted = Now;
        }

        if (TekStriderElementShardMode) {
            ReleaseAutoHarvestSprint();
            ReleaseAutoHarvestAttack();
            if (!s_autoHarvestState.TekStriderBeamActive &&
                Now >= s_autoHarvestState.NextAttack) {
                if (SendAutoHarvestTekStriderRigToggle()) {
                    s_autoHarvestState.TekStriderBeamActive = true;
                }
                s_autoHarvestState.NextAttack = Now + std::chrono::milliseconds(750);
            }

            std::ostringstream Status;
            Status << (s_autoHarvestState.TekStriderBeamActive
                ? "跨步者采集钻头已开启，正在采集 "
                : "正在瞄准并启动跨步者采集钻头 ")
                << s_autoHarvestState.TargetType << " ("
                << std::fixed << std::setprecision(1) << DistanceMeters << "m)";
            SetAutoHarvestStatus(Status.str());
            return;
        }

        const int AttackIntervalMs = static_cast<int>(std::clamp(
            g_Config::AutoHarvestAttackIntervalMs, 200.0f, 2500.0f));
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

        // Auto harvesting writes the desired resource-facing rotation to the
        // controller. Using the third-person camera rotation here can fight that
        // target (especially while swimming) and make the mount circle in place.
        SDK::FRotator rot = g_Config::bAutoHarvestResources
            ? LocalPC->GetControlRotation()
            : LocalPC->PlayerCameraManager->GetCameraRotation();

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

    void AutoSwapBrokenEquipment()
    {
        static int globalTick = 0;
        globalTick++;

        if (globalTick % 3 != 0) return;

        SDK::AShooterPlayerController* PC = (SDK::AShooterPlayerController*)g_Util::GetLocalPC();
        if (!PC || !PC->Pawn) return;

        auto Character = (SDK::APrimalCharacter*)PC->Pawn;
        if (!Character || !Character->MyInventoryComponent) return;

        auto Inv = Character->MyInventoryComponent;
        auto& EquippedItems = Inv->EquippedItems;
        auto& InventoryItems = Inv->InventoryItems;

        if (InventoryItems.Num() == 0) return;

        static int lastSwapTick[25] = { 0 };
        const float ALERT_DURABILITY = g_Config::ArmorRange;

        //获取当前穿着的装备
        SDK::UPrimalItem* WornArmor[20] = { nullptr };
        for (int i = 0; i < EquippedItems.Num(); i++)
        {
            auto item = EquippedItems[i];
            if (!item) continue;

            int typeIndex = (int)item->MyEquipmentType;
            if (typeIndex >= 0 && typeIndex < 20)
            {
                WornArmor[typeIndex] = item;
            }
        }

        int targetSlots[] = { 0, 1, 2, 3, 4, 8 };
        bool bSlotNeedsReplace[25] = { false }; // 记录需换甲
        bool bAnyNeedsReplace = false;          // 是否需要换

        for (int i = 0; i < 6; i++)
        {
            int typeIndex = targetSlots[i];

            // 如果在冷却中，直接跳过，本帧不处理它
            if (globalTick - lastSwapTick[typeIndex] < 60) continue;

            auto CurrentEquippedItem = WornArmor[typeIndex];

            if (CurrentEquippedItem == nullptr)
            {
                // 空槽位，需要穿
                bSlotNeedsReplace[typeIndex] = true;
                bAnyNeedsReplace = true;
            }
            else
            {
                // 有装备，判断是否快碎了
                if (!CurrentEquippedItem->bIsItemSkin && CurrentEquippedItem->bUseItemDurability)
                {
                    if (CurrentEquippedItem->ItemDurability <= ALERT_DURABILITY)
                    {
                        bSlotNeedsReplace[typeIndex] = true;
                        bAnyNeedsReplace = true;
                    }
                }
            }
        }

        //如果没有需要换的 跳过
        if (!bAnyNeedsReplace) return;

        float bestDurabilityForSlot[25] = { -1.0f };         // 找到的最高耐久
        SDK::FItemNetID* bestItemNetIdForSlot[25] = { nullptr }; // 记录最高耐久装备的ID

        // 只遍历一次大背包
        for (int j = 0; j < InventoryItems.Num(); j++)
        {
            auto CandItem = InventoryItems[j];
            if (!CandItem) continue;

            int typeIndex = (int)CandItem->MyEquipmentType;

            if (typeIndex >= 0 && typeIndex < 25 && bSlotNeedsReplace[typeIndex])
            {
                if (CandItem->bIsItemSkin || !CandItem->bUseItemDurability) continue;
                if (CandItem->bIsBlueprint || CandItem->bIsEngram) continue;

                float candDur = CandItem->ItemDurability;
                if (candDur > ALERT_DURABILITY && candDur > bestDurabilityForSlot[typeIndex])
                {
                    bestDurabilityForSlot[typeIndex] = candDur;
                    bestItemNetIdForSlot[typeIndex] = &CandItem->ItemID;
                }
            }
        }

        for (int i = 0; i < 6; i++)
        {
            int typeIndex = targetSlots[i];

            if (bSlotNeedsReplace[typeIndex])
            {
                if (bestItemNetIdForSlot[typeIndex] != nullptr)
                {
                    PC->ServerEquipPawnItem(*bestItemNetIdForSlot[typeIndex]);
                    lastSwapTick[typeIndex] = globalTick; 
                }
                else
                {
                    lastSwapTick[typeIndex] = globalTick;
                }
            }
        }
    }

    void OutBody()
    {
        struct OutBodyState {
            SDK::AShooterCharacter* Character = nullptr;
            int32_t CharacterObjectIndex = -1;
            SDK::UCharacterMovementComponent* Movement = nullptr;
            int32_t MovementObjectIndex = -1;
            SDK::FVector SavedLocation{};
            SDK::EMovementMode OriginalMovementMode = SDK::EMovementMode::MOVE_Walking;
            uint8_t OriginalCustomMovementMode = 0;
            float OriginalMaxFlySpeed = 0.0f;
            float OriginalMaxAcceleration = 0.0f;
            float OriginalBrakingDecelerationFlying = 0.0f;
            bool OriginalCollisionEnabled = true;
            bool Active = false;
        };

        static OutBodyState State;

        auto ClearStateWithoutTouchingPawn = []() {
            State = OutBodyState{};
            g_MDX12::g_MenuState::g_isOutBodyActive = false;
        };

        auto RestoreState = [&ClearStateWithoutTouchingPawn](bool RestoreLocation) {
            SDK::AShooterCharacter* ValidCharacter = State.Active
                ? ResolveCachedObject(State.Character, State.CharacterObjectIndex)
                : nullptr;
            SDK::UCharacterMovementComponent* ValidMovement =
                ResolveCachedObject(State.Movement, State.MovementObjectIndex);

            if (ValidCharacter && ValidMovement) {
                // Move back while collision is still disabled, then restore
                // every value captured on entry. ReplicateMovement is no
                // longer changed by this feature.
                if (RestoreLocation) {
                    ValidCharacter->K2_SetActorLocation(State.SavedLocation, false, nullptr, true);
                }
                WriteReflectedTimeValue(ValidMovement, "MaxFlySpeed", State.OriginalMaxFlySpeed);
                WriteReflectedTimeValue(ValidMovement, "MaxAcceleration", State.OriginalMaxAcceleration);
                WriteReflectedTimeValue(ValidMovement, "BrakingDecelerationFlying",
                    State.OriginalBrakingDecelerationFlying);
                ValidMovement->SetMovementMode(State.OriginalMovementMode,
                    State.OriginalCustomMovementMode);
                ValidCharacter->SetActorEnableCollision(State.OriginalCollisionEnabled);
            }

            ClearStateWithoutTouchingPawn();
        };

        SDK::APlayerController* LocalPC = g_Util::GetLocalPC();
        SDK::AShooterCharacter* CurrentCharacter = nullptr;
        SDK::UCharacterMovementComponent* CurrentMovement = nullptr;
        if (LocalPC && LocalPC->Pawn &&
            LocalPC->Pawn->IsA(SDK::AShooterCharacter::StaticClass()) &&
            !LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            CurrentCharacter = static_cast<SDK::AShooterCharacter*>(LocalPC->Pawn);
            SDK::UPawnMovementComponent* PawnMovement = LocalPC->Pawn->GetMovementComponent();
            if (PawnMovement && PawnMovement->IsA(SDK::UCharacterMovementComponent::StaticClass())) {
                CurrentMovement = static_cast<SDK::UCharacterMovementComponent*>(PawnMovement);
            }
        }

        const bool Requested = g_Config::bOutBodyChecked &&
            g_MDX12::g_MenuState::g_isOutBodyActive;

        if (State.Active) {
            const bool SamePawn = CurrentCharacter == State.Character &&
                CurrentCharacter && CurrentCharacter->Index == State.CharacterObjectIndex &&
                CurrentMovement == State.Movement && CurrentMovement &&
                CurrentMovement->Index == State.MovementObjectIndex;

            if (!SamePawn) {
                // Disconnect, respawn, spectator transition, mounting, or a
                // server change: restore the old pawn if it is still registered,
                // but never apply its saved location to the new pawn.
                RestoreState(false);
                return;
            }
            if (!CurrentCharacter->HasAuthority()) {
                // Client-side movement/collision changes are corrected by the
                // server and can also break interaction traces. This mode is
                // therefore limited to single-player/listen-server authority.
                RestoreState(false);
                return;
            }
            if (!Requested) {
                RestoreState(true);
                return;
            }

            uint8_t CurrentMode = 0;
            if (!ReadReflectedByteValue(State.Movement, "MovementMode", &CurrentMode) ||
                CurrentMode != static_cast<uint8_t>(SDK::EMovementMode::MOVE_Flying)) {
                State.Movement->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);
            }
            return;
        }

        if (!Requested) return;
        if (!CurrentCharacter || !CurrentMovement || !CurrentCharacter->HasAuthority()) {
            g_MDX12::g_MenuState::g_isOutBodyActive = false;
            return;
        }

        uint8_t OriginalMovementMode = 0;
        uint8_t OriginalCustomMovementMode = 0;
        float OriginalMaxFlySpeed = 0.0f;
        float OriginalMaxAcceleration = 0.0f;
        float OriginalBrakingDecelerationFlying = 0.0f;
        if (!ReadReflectedByteValue(CurrentMovement, "MovementMode", &OriginalMovementMode) ||
            !ReadReflectedByteValue(CurrentMovement, "CustomMovementMode", &OriginalCustomMovementMode) ||
            !ReadReflectedFloatValue(CurrentMovement, "MaxFlySpeed", &OriginalMaxFlySpeed) ||
            !ReadReflectedFloatValue(CurrentMovement, "MaxAcceleration", &OriginalMaxAcceleration) ||
            !ReadReflectedFloatValue(CurrentMovement, "BrakingDecelerationFlying",
                &OriginalBrakingDecelerationFlying)) {
            g_MDX12::g_MenuState::g_isOutBodyActive = false;
            return;
        }

        State.Character = CurrentCharacter;
        State.CharacterObjectIndex = CurrentCharacter->Index;
        State.Movement = CurrentMovement;
        State.MovementObjectIndex = CurrentMovement->Index;
        State.SavedLocation = CurrentCharacter->K2_GetActorLocation();
        State.OriginalMovementMode = static_cast<SDK::EMovementMode>(OriginalMovementMode);
        State.OriginalCustomMovementMode = OriginalCustomMovementMode;
        State.OriginalMaxFlySpeed = OriginalMaxFlySpeed;
        State.OriginalMaxAcceleration = OriginalMaxAcceleration;
        State.OriginalBrakingDecelerationFlying = OriginalBrakingDecelerationFlying;
        State.OriginalCollisionEnabled = CurrentCharacter->GetActorEnableCollision();
        State.Active = true;

        CurrentCharacter->SetActorEnableCollision(false);
        WriteReflectedTimeValue(CurrentMovement, "MaxFlySpeed", 10000.0);
        WriteReflectedTimeValue(CurrentMovement, "MaxAcceleration", 5000.0);
        WriteReflectedTimeValue(CurrentMovement, "BrakingDecelerationFlying", 9999.0);
        CurrentMovement->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);
    }
}
