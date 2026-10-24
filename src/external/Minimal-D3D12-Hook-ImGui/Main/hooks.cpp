#include "../../../internal/Menu/VibrancyBackdrop.h"
#include "mdx12_api.h"
#include "hooks.h"
#include "UnloadBarrier.h"
#include "RespawnNotification.h"
#include "../../AOBScan/AOBScan.hpp"
#include "../../../internal/CheatData/CheatData.hpp"
#include "../../../internal/Config/Configs.h"
// #include "../Font/Alibaba-PuHuiTi-Bold.h"
// #include "../Font/Alibaba-PuHuiTi-Heavy.h"
// #include "../Font/Alibaba-PuHuiTi-Light.h"
// #include "../Font/Alibaba-PuHuiTi-Medium.h"
// #include "../Font/Alibaba-PuHuiTi-Regular.h"

#include "../MinHook/include/MinHook.h"
#include "../../ezVMT/include/ezVMT.hpp"

#include "../../../internal/Util/Util.h"
#include "../../../internal/ESP/DrawESP.h"
#include "../../../internal/Hack/Hack.h"
#include "../../../internal/Hack/TekSpear.h"
#include "../../../internal/Legit/Aimbot/Aimbot.h"
#include "../../../internal/Legit/Aimbot/AimViewArguments.h"
#include "../../../internal/Log/LogManager.h"
#include "../../../internal/Language/LanguageManager.h"
#include "../../../internal/Lua/LuaManager.h"

#include "../../../internal/Menu/ConfigImGui.h"
#include "../../../internal/Menu/ShortcutRuntime.h"

#include <cstring>
#include <array>
#include <cmath>
#include <TlHelp32.h>
#include <wrl/client.h>

#pragma warning(push)
#pragma warning(disable: 26451)
#pragma warning(disable: 26812)
#pragma warning(disable: 6387)
#pragma warning(pop)

namespace {
    std::atomic<uint32_t> g_ActiveHookCalls{ 0 };
    std::atomic<bool> g_UnloadRequested{ false };
    std::atomic<bool> g_RuntimeReady{ false };
    std::atomic<bool> g_UnloadWorkerActive{ false };
    std::atomic<HANDLE> g_InitializationThread{ nullptr };
    std::recursive_mutex g_GameRuntimeMutex;
    std::atomic<HMODULE> g_ModuleHandle{ nullptr };
    std::atomic<bool> g_RenderStateResetStarted{ false };
    std::atomic<bool> g_RenderStateResetComplete{ false };
    std::atomic_flag g_UnloadRequestGate = ATOMIC_FLAG_INIT;

    DWORD WINAPI UnloadWorker(LPVOID parameter);

    bool ShouldBypassFeatures() {
        return !g_RuntimeReady.load(std::memory_order_acquire) ||
            g_UnloadRequested.load(std::memory_order_acquire);
    }

    void PrepareRuntimeForUnload() {
        g_Config::bSuperFlyer = false;
        g_Config::bForceTurn = false;
        g_Config::bFreeCameraEnabled = false;
        g_Config::bPumpShotgunNoReload = false;
        // 秒换弹与弓秒蓄力：会修改当前武器的换弹/蓄力字段，卸载前必须恢复。
        g_Config::bInstantReload = false;
        g_Config::bFastBow = false;
        g_Config::bAutoOpenGachaCrystals = false;
        g_Config::bAutoPullGachaCrystals = false;
        g_Config::bAutoDropResources = g_Config::bUnderwaterWeapons = false;
        g_Config::bAutoDepositGachaGear = false;
        g_Config::bAutoDepositFishingBlueprints = false;
        g_Config::bAutoFishingQTE = false;
        g_Config::bAutoFishingRecast = false;
        g_Config::bAutoEatDrink = false;
        g_Config::bAutoMedicalBrew = false;
        g_Config::bAutoEnergyBrew = false;
        g_Config::bAutoBuffSoup = false;
        g_Config::bAutoFireExtinguish = false;
        g_Config::bInstantThrowables = false;
        g_Config::bAutoSwitchFishingRod = false;
        g_Config::bAutoRecoverFishingBait = false;
        g_Config::bAutoPickupDroppedItems = false;
        g_Config::bAutoLootContainers = false;
        g_Config::bWeaponFireRate = false;
        g_Config::bProjectilePrediction = false;
        g_Config::bAutoCraft = false;
        g_Config::bAutoRepairItems = false;
        g_Config::bAutoHarvestResources = false;
        g_Config::bDinoHarvestRange = false;
        SDK::UWorld* World = SDK::UWorld::GetWorld();
        g_Hack::ResetFreeCamera();
        g_Hack::ResetWorldUtilities();
        g_Hack::SuperFlyer(World);
        g_Hack::PumpShotgunNoReload(World);
        g_Hack::AutoOpenGachaCrystals(World);
        g_Hack::AutoFishingQTE(World);
        g_Hack::AutoEatDrink(World);
        g_Hack::AutoUseBrews(World);
        // 灭火流程会临时改视角并换武器，卸载前必须回正并恢复状态。
        g_Hack::AutoFireExtinguish(World);
        // 手雷物品的“免举起”位段恢复。
        g_Hack::ResetInstantThrowables();
        // 秒换弹（换弹时长字段）与弓秒蓄力（拉弓时间字段）恢复。
        g_Hack::ResetInstantReload();
        g_Hack::MaintainFishingRod(World);
        // Auto-harvest owns a persistent Strider beam. Its cleanup is
        // acknowledged from the UE game-thread unload handshake.
        g_Hack::ForceTurn(nullptr, 0.0f);
    }

    void TryResetRenderStateForUnload() {
        std::scoped_lock RuntimeLock(g_GameRuntimeMutex);
        if (!g_UnloadRequested.load(std::memory_order_acquire) ||
            g_RenderStateResetComplete.load(std::memory_order_acquire)) {
            return;
        }

        bool Expected = false;
        if (!g_RenderStateResetStarted.compare_exchange_strong(Expected, true,
            std::memory_order_acq_rel)) {
            return;
        }

        // This helper is called only by UpdateCamera/PostRender, which are
        // serialized on the UE game thread. Unrelated D3D/input callbacks must
        // not prevent the restoration from running.
        PrepareRuntimeForUnload();
        if (g_Hack::IsFreeCameraActive()) {
            // Keep the game-thread callback installed until the native debug
            // controller has handed input back to the survivor controller.
            g_RenderStateResetStarted.store(false, std::memory_order_release);
            return;
        }
        if (!g_Hack::ResetAutoHarvestForUnload()) {
            g_RenderStateResetStarted.store(false, std::memory_order_release);
            return;
        }
        g_Hack::ResetGameplayExtensions();
        g_Aimbot::Reset();
        if (!g_TekSpear::Reset()) {
            g_RenderStateResetStarted.store(false, std::memory_order_release);
            return;
        }
        g_DrawESP::ResetResourceModelHighlights();
        LuaManager::Get().SetEnabled(false);
        g_MDX12::g_MenuState::g_isOpen = false;
        g_MDX12::cursorhook::RestoreForUnload();
        g_RenderStateResetComplete.store(true, std::memory_order_release);
    }

    bool IsExecutableGameAddress(void* Address) {
        if (!Address) return false;

        MEMORY_BASIC_INFORMATION MemoryInfo{};
        if (VirtualQuery(Address, &MemoryInfo, sizeof(MemoryInfo)) !=
            sizeof(MemoryInfo) || MemoryInfo.State != MEM_COMMIT ||
            (MemoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }

        const DWORD BaseProtection = MemoryInfo.Protect & 0xFFu;
        const bool IsExecutable = BaseProtection == PAGE_EXECUTE ||
            BaseProtection == PAGE_EXECUTE_READ ||
            BaseProtection == PAGE_EXECUTE_READWRITE ||
            BaseProtection == PAGE_EXECUTE_WRITECOPY;
        if (!IsExecutable) return false;

        HMODULE GameModule = GetModuleHandleW(nullptr);
        if (!GameModule) return false;
        const auto* DosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(GameModule);
        if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* NtHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const uint8_t*>(GameModule) + DosHeader->e_lfanew);
        if (NtHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

        const uintptr_t ImageBegin = reinterpret_cast<uintptr_t>(GameModule);
        const uintptr_t ImageEnd = ImageBegin + NtHeaders->OptionalHeader.SizeOfImage;
        const uintptr_t Target = reinterpret_cast<uintptr_t>(Address);
        return Target >= ImageBegin && Target < ImageEnd;
    }

    bool IsExpectedUpdateCameraAddress(void* Address) {
        if (!IsExecutableGameAddress(Address)) return false;

        MEMORY_BASIC_INFORMATION MemoryInfo{};
        if (VirtualQuery(Address, &MemoryInfo, sizeof(MemoryInfo)) !=
            sizeof(MemoryInfo)) {
            return false;
        }
        const uintptr_t Target = reinterpret_cast<uintptr_t>(Address);
        const uintptr_t RegionEnd = reinterpret_cast<uintptr_t>(MemoryInfo.BaseAddress) +
            MemoryInfo.RegionSize;
        if (Target > RegionEnd || RegionEnd - Target < 0x80) return false;

        const auto* Code = static_cast<const uint8_t*>(Address);
        // ASA CL 832196 UpdateCamera(float) prologue. Stack-frame displacements are
        // deliberately ignored, while the register-save shape is required.
        constexpr uint8_t Prologue[] = {
            0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57,
            0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
            0x49, 0x8D, 0xAB, 0x00, 0x00, 0xFF, 0xFF,
            0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00,
            0x41, 0x0F, 0x29, 0x73, 0xB8,
            0x41, 0x0F, 0x29, 0x7B, 0xA8
        };
        constexpr char Mask[] =
            "xxxxxxxxxxxxxxxx??xxxxx??xxxxxxxxxxxx";
        static_assert(sizeof(Prologue) + 1 == sizeof(Mask));
        for (size_t Index = 0; Index < sizeof(Prologue); ++Index) {
            if (Mask[Index] == 'x' && Code[Index] != Prologue[Index]) return false;
        }

        constexpr uint8_t CameraOwnerRead[] = {
            0x4C, 0x8B, 0xB1, 0x90, 0x04, 0x00, 0x00
        };
        constexpr uint8_t DeltaTimeSave[] = { 0x44, 0x0F, 0x28, 0xE9 };
        const auto Contains = [Code](const uint8_t* Needle, size_t NeedleSize) {
            for (size_t Offset = 0; Offset + NeedleSize <= 0x80; ++Offset) {
                if (std::memcmp(Code + Offset, Needle, NeedleSize) == 0) return true;
            }
            return false;
        };
        return Contains(CameraOwnerRead, sizeof(CameraOwnerRead)) &&
            Contains(DeltaTimeSave, sizeof(DeltaTimeSave));
    }
}

namespace g_Hook {
    CallbackScope::CallbackScope() {
        g_ActiveHookCalls.fetch_add(1, std::memory_order_acq_rel);
    }

    CallbackScope::~CallbackScope() {
        g_ActiveHookCalls.fetch_sub(1, std::memory_order_acq_rel);
    }

    typedef void (__fastcall* tActorTick)(SDK::AActor* rcx, float DeltaTime);
    tActorTick oActorTick = nullptr;

    typedef void (__fastcall* tHandleDisconnectFunction)(SDK::UNetConnection* rcx, void* rdx);
    tHandleDisconnectFunction oHandleDisconnect = nullptr;

    typedef void(__fastcall* tOutputTextLine)(SDK::UConsole* Console, SDK::FString* Message, void* r8, void* r9);
    tOutputTextLine oOutputTextLine = nullptr;

    typedef void(__fastcall* tPostRender)(SDK::UGameViewportClient* rcx, SDK::UCanvas* canvas);
    tPostRender oPostRender = nullptr;

    typedef void(__fastcall* tUpdateCamera)(
        SDK::AShooterPlayerCameraManager* CameraManager, float DeltaTime);
    tUpdateCamera oUpdateCamera = nullptr;

    typedef void(__fastcall* tPhysicsRotation)(SDK::UMovementComponent* rcx, float DeltaTime);
    tPhysicsRotation oPhysicsRotation = nullptr;

    /*
    typedef float(__fastcall* tTakeDamage)(
        SDK::AActor* _this,             // RCX
        float DamageAmount,             // XMM1
        SDK::FDamageEvent* DamageEvent, // R8
        SDK::AController* Instigator,   // R9
        SDK::AActor* DamageCauser       // 栈上
        );

    tTakeDamage oTakeDamage = nullptr;
    */

    typedef void(__fastcall* tClientNotifyReconnected)(SDK::APrimalPlayerController* rcx, SDK::APawn* NewPawn);
    tClientNotifyReconnected oClientNotifyReconnected = nullptr;

    typedef void(__fastcall* tClientNotifyRespawned)(SDK::APrimalPlayerController* rcx, SDK::APawn* NewPawn, bool IsFirstSpawn);
    tClientNotifyRespawned oClientNotifyRespawned = nullptr;

    typedef void(__fastcall* tClientChatMessage)(SDK::AShooterPlayerController* rcx, SDK::FPrimalChatMessage& Chat);
    tClientChatMessage oClientChatMessage = nullptr;

    typedef void(__fastcall* tClientAddFloatingDamageText)(
        SDK::AShooterPlayerController* _this,
        SDK::FVector_NetQuantize& AtLocation,
        SDK::int32 DamageAmount,
        SDK::int32 FromTeamID
        );

    tClientAddFloatingDamageText oClientAddFloatingDamageText = nullptr;

    void __fastcall hkActorTick(SDK::AActor* rcx, float DeltaTime) {
        CallbackScope Guard;
        if (ShouldBypassFeatures()) {
            // Actor ticks may opt into task-thread execution. UObject/component
            // restoration is therefore left to UpdateCamera or PostRender,
            // which are serialized on the UE game thread.
            return oActorTick(rcx, DeltaTime);
        }
        // g_LogManager::AddLog(255, 255, 255, 255, std::format("[{}]", DeltaTime));

        {
            std::scoped_lock RuntimeLock(g_GameRuntimeMutex);
            if (!ShouldBypassFeatures()) g_MDX12::SetupActorTick(rcx);
        }
        oActorTick(rcx, DeltaTime);
    }

    void __fastcall hkHandleDisconnect(SDK::UNetConnection* rcx, void* rdx) {
        CallbackScope Guard;
        if (!ShouldBypassFeatures()) g_MDX12::SetupHandleDisconnect(rcx);

        return oHandleDisconnect(rcx, rdx);
    }

    void __fastcall hkOutputTextLine(SDK::UConsole* rcx, SDK::FString* Message, void* r8, void* r9) {
        CallbackScope Guard;
        if (!ShouldBypassFeatures()) g_MDX12::SetupOutputTextLine(rcx, Message);

        return oOutputTextLine(rcx, Message, r8, r9);

        // 应该不会返回空指针
        /*
        if (rcx && Message) {
            g_MDX12::SetupOutputTextLine(rcx, Message);
        }

        if (oOutputTextLine) {
            oOutputTextLine(rcx, Message, r8, r9);
        }
        */
    }

    using AdjustedAimFunction = SDK::FVector*(__fastcall*)(SDK::AShooterWeapon*, SDK::FVector*);
    std::array<AdjustedAimFunction, 4> oAdjustedAim{};
    std::array<void*, 4> AdjustedAimTargets{};
    size_t AdjustedAimSlot = 0;

    template<size_t Index>
    SDK::FVector* __fastcall hkAdjustedAim(SDK::AShooterWeapon* Weapon, SDK::FVector* Result) {
        CallbackScope Guard;
        auto* Direction = oAdjustedAim[Index](Weapon, Result);
        if (!ShouldBypassFeatures() && Direction)
            g_Aimbot::OverrideWeaponAim(Weapon, *Direction);
        return Direction;
    }

    bool SupportsWeaponAim(SDK::AShooterWeapon* Weapon) {
        if (!WeaponAimOK || !Weapon) return false;
        bool SupportedFamily = Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass()) ||
            Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass());
        // The Tek phase pistol may use a blueprint beam weapon rather than
        // ShooterWeapon_Instant. Match its actual asset ancestry as well.
        for (auto* Class = Weapon->Class; !SupportedFamily && Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct)) {
            const auto Name = Class->GetName();
            SupportedFamily = Name == "WeapTekPistol_C" || Name == "WeapTekBow_C";
        }
        if (!SupportedFamily) return false;
        const auto Target = (*reinterpret_cast<void***>(Weapon))[AdjustedAimSlot];
        for (auto* Installed : AdjustedAimTargets)
            if (Installed && Target == Installed) return true;
        return false;
    }

    // 投掷物瞬间扔出：拦截 ShooterWeapon.StartFire 的 UFUNCTION 包装（所有武器
    // 共用入口），仅对本地人物当前手持的手雷类武器直接发送原生开火 RPC，并吞掉
    // 原生开火流程（跳过蓄力/松键投掷），避免二次投掷。
    bool IsUsableObject(const SDK::UObject* Object) {
        if (!Object || !Object->Class || Object->IsDefaultObject()) return false;
        if (Object->Flags & SDK::EObjectFlags::BeginDestroyed) return false;
        if (Object->Flags & SDK::EObjectFlags::FinishDestroyed) return false;
        if (Object->Flags & SDK::EObjectFlags::MirroredGarbage) return false;
        return true;
    }

    // 手雷类投掷武器判据。泰克榴弹发射器不在 SDK 内（Genesis DLC 蓝图），
    // 只能按类名/关联物品类名识别，因此单独用一个开关放行。
    SDK::FProperty* FindClassProperty(const SDK::UStruct* Type, const char* Name) {
        if (!Type || !Name) return nullptr;
        for (auto* Current = Type; Current; Current = Current->SuperStruct) {
            for (auto* Field = Current->ChildProperties; Field; Field = Field->Next) {
                if (!Field->ClassPrivate) continue;
                if (Field->Name.ToString() != Name) continue;
                auto* Property = static_cast<SDK::FProperty*>(Field);
                if (Property->Offset < 0 || Property->ArrayDim != 1 ||
                    Property->ElementSize <= 0 || Property->Offset > Type->Size) return nullptr;
                return Property;
            }
        }
        return nullptr;
    }

    bool IsThrowableGrenadeWeapon(SDK::UObject* Weapon) {
        if (!IsUsableObject(Weapon) || !Weapon->Class) return false;
        if (Weapon->IsA(SDK::APrimalWeaponGrenade::StaticClass())) return true;
        if (!g_Config::bInstantThrowLauncher) return false;
        auto Contains = [](std::string Value, const char* Needle) {
            for (auto& C : Value) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
            return Value.find(Needle) != std::string::npos;
        };
        for (auto* Class = Weapon->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
            if (Contains(Class->GetName(), "grenadelauncher")) return true;
        SDK::FProperty* Property = FindClassProperty(Weapon->Class, "AssociatedPrimalItem");
        if (!Property || !Property->ClassPrivate ||
            Property->ElementSize < int(sizeof(SDK::UObject*))) return false;
        if (Property->ClassPrivate->Name.ToString() != "ObjectProperty") return false;
        auto* Item = *reinterpret_cast<SDK::UObject**>(
            reinterpret_cast<uint8_t*>(Weapon) + Property->Offset);
        return IsUsableObject(Item) && Item->Class && Contains(Item->Class->GetName(), "grenadelauncher");
    }

    using StartFireExecFunction = void(__fastcall*)(SDK::UObject*, void*);
    StartFireExecFunction oStartFire = nullptr;

    using GrenadeStartFireFunction = void(__fastcall*)(SDK::APrimalWeaponGrenade*, bool);
    GrenadeStartFireFunction oGrenadeStartFire = nullptr;
    std::atomic<uint32_t> InstantThrowGrenadeCalls{0};
    std::atomic<uint32_t> InstantThrowGrenadeReleases{0};

    void __fastcall hkGrenadeStartFire(SDK::APrimalWeaponGrenade* Weapon, bool FromGamepad) {
        CallbackScope Guard;
        ++InstantThrowGrenadeCalls;
        // Preserve native CanFire, ammo checks and the server start notification.
        oGrenadeStartFire(Weapon, FromGamepad);
        if (ShouldBypassFeatures() || !g_Config::bInstantThrowables || !IsUsableObject(Weapon)) return;
        auto* PC = g_Util::GetLocalPC();
        auto* Human = PC && PC->IsA(SDK::AShooterPlayerController::StaticClass())
            ? static_cast<SDK::AShooterPlayerController*>(PC)->GetPlayerCharacter() : nullptr;
        if (!Human || Human->CurrentWeapon != Weapon || !Weapon->bIsCookingGrenade) return;
        // StopFire consumes the cooking state once and retains native projectile creation.
        // A later physical release cannot consume that same cooking state again.
        Weapon->StopFire();
        ++InstantThrowGrenadeReleases;
    }

    bool InitGrenadeStartFire() {
        auto* Default = SDK::APrimalWeaponGrenade::GetDefaultObj();
        if (!Default) return false;
        // Verified StartFire thunk dispatches through byte slot 0xf78 in this build.
        auto* Target = (*reinterpret_cast<void***>(Default))[0xf78 / sizeof(void*)];
        if (!IsExecutableGameAddress(Target)) return false;
        constexpr uint8_t Prefix[] = {0x40,0x57,0x48,0x83,0xec,0x50,0xf6,0x81,0xc8,0x0c,0,0,2};
        if (std::memcmp(Target, Prefix, sizeof(Prefix)) != 0) return false;
        if (MH_CreateHook(Target, reinterpret_cast<void*>(&hkGrenadeStartFire),
            reinterpret_cast<void**>(&oGrenadeStartFire)) != MH_OK) return false;
        return MH_EnableHook(Target) == MH_OK;
    }

    // 弓秒蓄力加强：满蓄力时按「已满蓄力」直接出箭，跳过松键等待。
    // 与投掷物共用同一 StartFire 包装入口，因此合并到 hkStartFire 里分支处理，
    // 避免出现两个互斥的 MinHook 安装者。
    // 只在本地持有 authority 时生效：BPFireWeapon 只在本机结算开火，吞掉原生
    // StartFire 会让远程服务器收不到这次射击通知（无弹道、无伤害），因此非
    // authority 一律放行原生流程。
    bool HandleBowFullChargeShot(SDK::UObject* Weapon) {
        if (!g_Config::bFastBow || !g_Config::bFastBowForceShot) return false;
        if (!IsUsableObject(Weapon) || !Weapon->Class ||
            !Weapon->IsA(SDK::APrimalWeaponBow::StaticClass())) return false;
        auto* Bow = static_cast<SDK::APrimalWeaponBow*>(Weapon);
        if (Bow->Role != SDK::ENetRole::ROLE_Authority) return false;
        auto* PC = g_Util::GetLocalPC();
        auto* ShooterPC = PC && PC->IsA(SDK::AShooterPlayerController::StaticClass())
            ? static_cast<SDK::AShooterPlayerController*>(PC) : nullptr;
        auto* Human = ShooterPC ? ShooterPC->GetPlayerCharacter() : nullptr;
        if (!Human || Human->CurrentWeapon != Weapon) return false;
        if (!g_Hack::ShouldForceFullChargeShot(static_cast<SDK::AShooterWeapon*>(Weapon))) return false;
        // 仍走原生 BPFireWeapon：保留扣弹、冷却、命中校验与服务器结算，不伪造伤害。
        Bow->BPFireWeapon();
        return true;
    }

    void __fastcall hkStartFire(SDK::UObject* Weapon, void* Stack) {
        CallbackScope Guard;
        if (!ShouldBypassFeatures() && g_Config::bInstantThrowables &&
            IsThrowableGrenadeWeapon(Weapon) &&
            !Weapon->IsA(SDK::APrimalWeaponGrenade::StaticClass())) {
            auto* PC = g_Util::GetLocalPC();
            auto* ShooterPC = PC && PC->IsA(SDK::AShooterPlayerController::StaticClass())
                ? static_cast<SDK::AShooterPlayerController*>(PC) : nullptr;
            auto* Human = ShooterPC ? ShooterPC->GetPlayerCharacter() : nullptr;
            if (Human && IsUsableObject(Weapon) && Human->CurrentWeapon == Weapon) {
                auto* Projectile = static_cast<SDK::AShooterWeapon_Projectile*>(Weapon);
                SDK::FVector Origin{}, ShootDir{};
                Projectile->BPGetProjectileSpawnTransform(&Origin, &ShootDir);
                if (std::isfinite(Origin.X) && std::isfinite(Origin.Y) && std::isfinite(Origin.Z) &&
                    std::isfinite(ShootDir.X) && std::isfinite(ShootDir.Y) && std::isfinite(ShootDir.Z)) {
                    SDK::FVector_NetQuantizeNormal Dir{};
                    Dir.X = ShootDir.X; Dir.Y = ShootDir.Y; Dir.Z = ShootDir.Z;
                    const int32_t ProjectileID = static_cast<int32_t>(GetTickCount64() & 0x7FFFFFFF);
                    Projectile->ServerFireProjectile(Origin, Dir, ProjectileID);
                    return; // 已立即投出，跳过原生开火
                }
            }
        }
        if (!ShouldBypassFeatures() && HandleBowFullChargeShot(Weapon))
            return; // 已按满蓄力出箭，跳过原生蓄力流程
        return oStartFire(Weapon, Stack);
    }

    void initInstantThrow() {
        InstantThrowOK = InitGrenadeStartFire();
        // 用类函数表定位（FindObject 全名含 /Script/ 包路径，容易失配）。
        auto* Default = SDK::AShooterWeapon::GetDefaultObj();
        if (!Default || !Default->Class) return;
        auto* Function = Default->Class->GetFunction("ShooterWeapon", "StartFire");
        if (!Function || !IsExecutableGameAddress(reinterpret_cast<void*>(Function->ExecFunction))) return;
        const auto* Code = reinterpret_cast<const uint8_t*>(Function->ExecFunction);
        // 校验 UFUNCTION 包装前缀（mov [rsp+10],rbx; mov [rsp+18],rsi; push rdi）。
        constexpr uint8_t Prefix[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
        if (std::memcmp(Code, Prefix, sizeof(Prefix)) != 0) return;
        void* Target = reinterpret_cast<void*>(Function->ExecFunction);
        if (MH_CreateHook(Target, reinterpret_cast<void*>(&hkStartFire),
            reinterpret_cast<void**>(&oStartFire)) != MH_OK) return;
        if (MH_EnableHook(Target) == MH_OK) BowFullChargeOK = true;
    }

    void initBowFullChargeShot() {
        // 与投掷物共用 ShooterWeapon.StartFire 包装入口，实际分支在 hkStartFire 内。
        g_Hook::BowFullChargeOK = oStartFire != nullptr && BowFullChargeOK;
    }

    using CharacterViewExecFunction = void(__fastcall*)(SDK::APrimalCharacter*, void*, void*);
    CharacterViewExecFunction oCharacterViewExec = nullptr;

    void OverrideConstructionView(SDK::UObject* Caller, SDK::UFunction* Node, uint8_t* Locals,
        const uint8_t* Arguments, SDK::APrimalCharacter* Character) {
        SDK::FVector Location{}, Direction{};
        if (!Node || !Locals || !Arguments || !g_Hack::GetFreeCameraBuildView(Caller, Character, Location, Direction)) return;
        // Use the same verified FFrame/script layout as the character-view hook.
        // Only two reflected local FVector outputs owned by this script are writable.
        struct ScriptStorage { const uint8_t* Data; int32_t Num, Max; } Script{};
        std::memcpy(&Script, reinterpret_cast<const uint8_t*>(Node) + 0x60, sizeof(Script));
        const auto Begin = reinterpret_cast<uintptr_t>(Script.Data);
        const auto Position = reinterpret_cast<uintptr_t>(Arguments);
        if (!Script.Data || Script.Num <= 0 || Script.Num > Script.Max || Script.Max > 16 * 1024 * 1024 ||
            Position < Begin || Position - Begin > static_cast<size_t>(Script.Num)) return;
        g_Aimbot::ViewArgumentRefs Refs{};
        if (!g_Aimbot::ReadViewArgumentRefs({Script.Data, static_cast<size_t>(Script.Num)}, Position - Begin, Refs)) return;
        const auto FindVector = [&](uintptr_t Ref) -> SDK::FProperty* {
            auto* Field = Node->ChildProperties;
            for (int Count = 0; Field && Count < 1024; ++Count, Field = Field->Next) {
                if (reinterpret_cast<uintptr_t>(Field) != Ref) continue;
                if (!Field->ClassPrivate || Field->ClassPrivate->Name.ToString() != "StructProperty") return nullptr;
                auto* Property = static_cast<SDK::FStructProperty*>(Field);
                return Property->Struct && Property->Struct->GetName() == "Vector" &&
                    g_Aimbot::ValidViewVectorOffset(Property->Offset, Property->ElementSize, Property->ArrayDim, Node->Size)
                    ? Property : nullptr;
            }
            return nullptr;
        };
        const auto* LocationProperty = FindVector(Refs.Location);
        const auto* DirectionProperty = FindVector(Refs.Direction);
        if (!LocationProperty || !DirectionProperty ||
            std::abs(LocationProperty->Offset - DirectionProperty->Offset) < sizeof(SDK::FVector)) return;
        std::memcpy(Locals + LocationProperty->Offset, &Location, sizeof(Location));
        std::memcpy(Locals + DirectionProperty->Offset, &Direction, sizeof(Direction));
    }

    void __fastcall hkCharacterViewExec(SDK::APrimalCharacter* Character, void* Stack, void* Result) {
        CallbackScope Guard;
        // FFrame starts with FOutputDevice (0x10). The reflected native wrapper
        // advances Code while reading its output references; preserve it first.
        struct FramePrefix {
            uint8_t OutputDevice[0x10];
            SDK::UFunction* Node;
            SDK::UObject* Object;
            const uint8_t* Code;
            uint8_t* Locals;
        } Frame{};
        static_assert(sizeof(FramePrefix) == 0x30);
        if (Stack) std::memcpy(&Frame, Stack, sizeof(Frame));
        oCharacterViewExec(Character, Stack, Result);
        if (!ShouldBypassFeatures()) {
            if (g_Hack::IsFreeCameraActive())
                OverrideConstructionView(Frame.Object, Frame.Node, Frame.Locals, Frame.Code, Character);
            else g_Aimbot::OverrideTekBowInstantView(Frame.Object, Frame.Node, Frame.Locals, Frame.Code, Character);
        }
    }

    void initTekBowInstantAim() {
        auto* Default = SDK::APrimalCharacter::GetDefaultObj();
        if (!Default) return;
        auto* Function = Default->Class->GetFunction("PrimalCharacter", "GetCharacterViewLocationAndDirection");
        if (!Function || !IsExecutableGameAddress(reinterpret_cast<void*>(Function->ExecFunction))) return;
        const auto* Code = reinterpret_cast<const uint8_t*>(Function->ExecFunction);
        // Verify FFrame Code/Object/Locals offsets against this wrapper before
        // using the frame prefix. Unsupported engine layouts leave it disabled.
        constexpr uint8_t CodeRead[] = {0x48,0x8b,0x42,0x20};
        constexpr uint8_t ObjectRead[] = {0x48,0x8b,0x4a,0x18};
        constexpr uint8_t LocalsRead[] = {0x4c,0x8b,0x42,0x28};
        if (!IsExecutableGameAddress(const_cast<uint8_t*>(Code + 0x98)) ||
            std::memcmp(Code + 0x14, CodeRead, sizeof(CodeRead)) != 0 ||
            std::memcmp(Code + 0x45, ObjectRead, sizeof(ObjectRead)) != 0 ||
            std::memcmp(Code + 0x93, LocalsRead, sizeof(LocalsRead)) != 0) return;
        void* Target = reinterpret_cast<void*>(Function->ExecFunction);
        if (MH_CreateHook(Target, reinterpret_cast<void*>(hkCharacterViewExec),
            reinterpret_cast<void**>(&oCharacterViewExec)) != MH_OK) return;
        if (MH_EnableHook(Target) == MH_OK) TekBowInstantAimOK = true;
        else {
            MH_RemoveHook(Target);
            oCharacterViewExec = nullptr;
        }
    }

    using ShotgunSpreadFunction = float(__fastcall*)(SDK::AShooterWeapon_Instant*);
    ShotgunSpreadFunction oShotgunSpread = nullptr;

    float __fastcall hkShotgunSpread(SDK::AShooterWeapon_Instant* Weapon) {
        CallbackScope Guard;
        float Spread = oShotgunSpread(Weapon);
        if (!ShouldBypassFeatures()) g_Aimbot::OverrideShotgunSpread(Weapon, Spread);
        return Spread;
    }

    void initShotgunSpread() {
        auto* Default = SDK::AShooterWeapon_Instant::GetDefaultObj();
        if (!Default) return;
        auto* Function = Default->Class->GetFunction("ShooterWeapon_Instant", "GetCurrentSpread");
        if (!Function || !IsExecutableGameAddress(reinterpret_cast<void*>(Function->ExecFunction))) return;
        const auto* Code = reinterpret_cast<const uint8_t*>(Function->ExecFunction);
        if (!IsExecutableGameAddress(const_cast<uint8_t*>(Code + 0x30))) return;
        auto* Target = reinterpret_cast<void*>(g_Aimbot::ResolveSpreadGetterTarget(
            {Code, 0x30}, reinterpret_cast<uintptr_t>(Code)));
        if (!IsExecutableGameAddress(Target) || !IsExecutableGameAddress(static_cast<uint8_t*>(Target) + 0x10)) return;
        // Verify the instant-weapon layout before hooking its native getter.
        constexpr uint8_t SpreadFlag[] = {0xf7,0x81,0x8c,0x11,0,0,0,0x04,0,0};
        if (std::memcmp(static_cast<uint8_t*>(Target) + 6, SpreadFlag, sizeof(SpreadFlag)) != 0) return;
        if (MH_CreateHook(Target, reinterpret_cast<void*>(hkShotgunSpread),
            reinterpret_cast<void**>(&oShotgunSpread)) != MH_OK) return;
        if (MH_EnableHook(Target) == MH_OK) ShotgunSpreadOK = true;
        else {
            MH_RemoveHook(Target);
            oShotgunSpread = nullptr;
        }
    }

    using ProjectileModifierFunction = float(__fastcall*)(SDK::AShooterWeapon_Projectile*);
    ProjectileModifierFunction oProjectileSpeedModifier = nullptr;
    ProjectileModifierFunction oProjectileGravityModifier = nullptr;

    void* ResolveProjectileModifierTarget(SDK::UFunction* Function, uint32_t ModifierIndex) {
        if (!Function || !IsExecutableGameAddress(reinterpret_cast<void*>(Function->ExecFunction))) return nullptr;
        const auto* Wrapper = reinterpret_cast<const uint8_t*>(Function->ExecFunction);
        constexpr uint8_t WrapperPrefix[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x42,0x20};
        constexpr uint8_t StoreResult[] = {0xf3,0x0f,0x11,0x03};
        if (!IsExecutableGameAddress(const_cast<uint8_t*>(Wrapper + 0x2c)) ||
            std::memcmp(Wrapper, WrapperPrefix, sizeof(WrapperPrefix)) != 0 || Wrapper[0x1e] != 0xe8 ||
            std::memcmp(Wrapper + 0x23, StoreResult, sizeof(StoreResult)) != 0 || Wrapper[0x2c] != 0xc3) return nullptr;
        int32_t Relative = 0;
        std::memcpy(&Relative, Wrapper + 0x1f, sizeof(Relative));
        auto* Target = const_cast<uint8_t*>(Wrapper + 0x23 + Relative);
        if (!IsExecutableGameAddress(Target) || !IsExecutableGameAddress(Target + 0x100)) return nullptr;
        // Current native body reads AssociatedPrimalItem at 0x710 and requests
        // Antigrav modifier index 0 (speed) or 1 (gravity). Reject updates that
        // change either contract instead of detouring an unverified address.
        constexpr uint8_t BodyPrefix[] = {
            0x48,0x8b,0xc4,0x48,0x89,0x58,0x20,0x57,0x48,0x81,0xec,0xc0,0,0,0,
            0x48,0x8b,0x99,0x10,0x07,0,0
        };
        constexpr uint8_t IndexWrite[] = {0xc7,0x44,0x24,0x20};
        uint32_t ActualIndex = UINT32_MAX;
        std::memcpy(&ActualIndex, Target + 0x7c, sizeof(ActualIndex));
        if (std::memcmp(Target, BodyPrefix, sizeof(BodyPrefix)) != 0 ||
            std::memcmp(Target + 0x78, IndexWrite, sizeof(IndexWrite)) != 0 || ActualIndex != ModifierIndex) return nullptr;
        return Target;
    }

    float ApplyProjectileMultiplier(float Native, float Configured, float Minimum, float Maximum) {
        if (!g_Config::bProjectileTrajectoryTuning || !std::isfinite(Native) || Native <= 0 ||
            !std::isfinite(Configured)) return Native;
        return Native * std::clamp(Configured, Minimum, Maximum);
    }
    float __fastcall hkProjectileSpeedModifier(SDK::AShooterWeapon_Projectile* Weapon) {
        CallbackScope Guard;
        const float Native = oProjectileSpeedModifier(Weapon);
        return ShouldBypassFeatures() ? Native : ApplyProjectileMultiplier(Native, g_Config::ProjectileSpeedMultiplier, 0.25f, 5.0f);
    }
    float __fastcall hkProjectileGravityModifier(SDK::AShooterWeapon_Projectile* Weapon) {
        CallbackScope Guard;
        const float Native = oProjectileGravityModifier(Weapon);
        return ShouldBypassFeatures() ? Native : ApplyProjectileMultiplier(Native, g_Config::ProjectileGravityMultiplier, 0.05f, 2.0f);
    }
    void initProjectileModifiers() {
        auto* Default = SDK::AShooterWeapon_Projectile::GetDefaultObj();
        if (!Default || !Default->Class) return;
        auto* SpeedFunction = Default->Class->GetFunction("ShooterWeapon_Projectile", "GetProjectileSpeedModifier");
        auto* GravityFunction = Default->Class->GetFunction("ShooterWeapon_Projectile", "GetProjectileGravityModifier");
        void* SpeedTarget = ResolveProjectileModifierTarget(SpeedFunction, 0);
        void* GravityTarget = ResolveProjectileModifierTarget(GravityFunction, 1);
        if (!SpeedTarget || !GravityTarget || SpeedTarget == GravityTarget) return;
        if (MH_CreateHook(SpeedTarget, reinterpret_cast<void*>(hkProjectileSpeedModifier),
            reinterpret_cast<void**>(&oProjectileSpeedModifier)) != MH_OK) return;
        if (MH_CreateHook(GravityTarget, reinterpret_cast<void*>(hkProjectileGravityModifier),
            reinterpret_cast<void**>(&oProjectileGravityModifier)) != MH_OK) {
            MH_RemoveHook(SpeedTarget); oProjectileSpeedModifier = nullptr; return;
        }
        if (MH_EnableHook(SpeedTarget) == MH_OK && MH_EnableHook(GravityTarget) == MH_OK) {
            ProjectileModifiersOK = true; return;
        }
        MH_DisableHook(SpeedTarget); MH_DisableHook(GravityTarget);
        MH_RemoveHook(SpeedTarget); MH_RemoveHook(GravityTarget);
        oProjectileSpeedModifier = nullptr; oProjectileGravityModifier = nullptr;
    }

    using ProjectileProcessEvent = void(__fastcall*)(SDK::UObject*, SDK::UFunction*, void*);
    ProjectileProcessEvent oProjectileProcessEvent = nullptr;
    void __fastcall hkProjectileProcessEvent(SDK::UObject* Object, SDK::UFunction* Function, void* Parameters) {
        CallbackScope Guard;
        if (!ShouldBypassFeatures() && g_Hack::SuppressTekHeatEvent(Object, Function)) return;
        if (!ShouldBypassFeatures() && g_Hack::HandlePlacementExperiment(Object, Function, Parameters, oProjectileProcessEvent)) return;
        if (!ShouldBypassFeatures() && g_Aimbot::IsProjectileShotEvent(Function))
            g_Aimbot::OverrideProjectileEvent(Object, Function, Parameters);
        if (!ShouldBypassFeatures() && g_Hack::SuppressDeinoFlightInput(Object,Function)) return;
        if (!ShouldBypassFeatures()) g_Hack::TraceDeinoActionEvent(Object,Function,Parameters,false);
        oProjectileProcessEvent(Object, Function, Parameters);
        if (!ShouldBypassFeatures()) g_Hack::TraceDeinoActionEvent(Object,Function,Parameters,true);
    }
    void initProjectileShotEvents() {
        auto* Default = SDK::AShooterWeapon_Projectile::GetDefaultObj();
        if (!Default || !g_Aimbot::InitializeProjectileEvents()) return;
        void* Target = (*reinterpret_cast<void***>(Default))[SDK::Offsets::ProcessEventIdx];
        if (!IsExecutableGameAddress(Target)) return;
        if (MH_CreateHook(Target, reinterpret_cast<void*>(hkProjectileProcessEvent),
            reinterpret_cast<void**>(&oProjectileProcessEvent)) != MH_OK) return;
        if (MH_EnableHook(Target) == MH_OK) ProjectileShotEventsOK = true;
        else { MH_RemoveHook(Target); oProjectileProcessEvent = nullptr; }
    }

    // Dinosaur and weapon ProcessEvent overrides are distinct in this build.
    // Trace actions; the unified flight controller consumes only its airborne jump inputs.
    ProjectileProcessEvent oDinoActionProcessEvent = nullptr;
    void __fastcall hkDinoActionProcessEvent(SDK::UObject* Object,SDK::UFunction* Function,void* Parameters) {
        CallbackScope Guard;
        if (!ShouldBypassFeatures() && g_Hack::SuppressDeinoFlightInput(Object,Function)) return;
        if (!ShouldBypassFeatures()) g_Hack::TraceDeinoActionEvent(Object,Function,Parameters,false);
        oDinoActionProcessEvent(Object,Function,Parameters);
        if (!ShouldBypassFeatures()) g_Hack::TraceDeinoActionEvent(Object,Function,Parameters,true);
    }
    void initDinoActionEvents() {
        auto* Default=SDK::APrimalDinoCharacter::GetDefaultObj();
        if (!Default) return;
        void* Target=(*reinterpret_cast<void***>(Default))[SDK::Offsets::ProcessEventIdx];
        if (!IsExecutableGameAddress(Target)) return;
        auto* WeaponDefault=SDK::AShooterWeapon_Projectile::GetDefaultObj();
        if (WeaponDefault && Target==(*reinterpret_cast<void***>(WeaponDefault))[SDK::Offsets::ProcessEventIdx]) {
            if (ProjectileShotEventsOK) { DinoActionEventTarget=Target; DinoActionEventsOK=true; }
            return;
        }
        if (MH_CreateHook(Target,reinterpret_cast<void*>(hkDinoActionProcessEvent),
            reinterpret_cast<void**>(&oDinoActionProcessEvent))!=MH_OK) return;
        if (MH_EnableHook(Target)==MH_OK) { DinoActionEventTarget=Target; DinoActionEventsOK=true; }
        else { MH_RemoveHook(Target); oDinoActionProcessEvent=nullptr; }
    }

    void initWeaponAim() {
        auto* Default = SDK::AShooterWeapon::GetDefaultObj();
        if (!Default) return;
        auto* Function = Default->Class->GetFunction("ShooterWeapon", "GetAdjustedAim");
        if (!Function || !IsExecutableGameAddress(reinterpret_cast<void*>(Function->ExecFunction))) return;
        const auto* Code = reinterpret_cast<const uint8_t*>(Function->ExecFunction);
        // Verify the reflected wrapper's FVector return ABI, then extract its
        // native virtual slot. Ordinary guns call this native method directly;
        // intercepting BPOverrideAimDirection through ProcessEvent misses it.
        constexpr uint8_t Prefix[] = {0x48,0x8d,0x54,0x24,0x20,0x48,0x8b,0x01,0xff,0x90};
        constexpr uint8_t Suffix[] = {0x0f,0x10,0x00,0x0f,0x11,0x03,0xf2,0x0f,0x10,0x48,0x10};
        if (!IsExecutableGameAddress(const_cast<uint8_t*>(Code + 0x40)) ||
            std::memcmp(Code + 0x1e, Prefix, sizeof(Prefix)) != 0 ||
            std::memcmp(Code + 0x2c, Suffix, sizeof(Suffix)) != 0) return;
        uint32_t Offset = 0;
        std::memcpy(&Offset, Code + 0x28, sizeof(Offset));
        if (Offset % sizeof(void*) || Offset < 0x800 || Offset > 0x2000) return;
        AdjustedAimSlot = Offset / sizeof(void*);
        SDK::AShooterWeapon* Defaults[] = {Default, SDK::AShooterWeapon_Instant::GetDefaultObj(),
            SDK::AShooterWeapon_Projectile::GetDefaultObj(), SDK::APrimalWeaponBow::GetDefaultObj()};
        AdjustedAimFunction Detours[] = {hkAdjustedAim<0>, hkAdjustedAim<1>, hkAdjustedAim<2>, hkAdjustedAim<3>};
        for (size_t Index = 0; Index < std::size(Defaults); ++Index) {
            if (!Defaults[Index]) continue;
            void* Target = (*reinterpret_cast<void***>(Defaults[Index]))[AdjustedAimSlot];
            if (!IsExecutableGameAddress(Target) ||
                std::find(AdjustedAimTargets.begin(), AdjustedAimTargets.end(), Target) != AdjustedAimTargets.end()) continue;
            if (MH_CreateHook(Target, reinterpret_cast<void*>(Detours[Index]),
                reinterpret_cast<void**>(&oAdjustedAim[Index])) != MH_OK) continue;
            if (MH_EnableHook(Target) == MH_OK) {
                AdjustedAimTargets[Index] = Target;
                WeaponAimOK = true;
            }
            else {
                MH_RemoveHook(Target);
                oAdjustedAim[Index] = nullptr;
            }
        }
    }

    void __fastcall hkPostRender(SDK::UGameViewportClient* rcx, SDK::UCanvas* canvas) {
        CallbackScope Guard;
        std::unique_lock<std::recursive_mutex> RuntimeLock(g_GameRuntimeMutex);
        if (ShouldBypassFeatures()) {
            PhoenixShortcuts::GameplayReady.store(false);
            PhoenixShortcuts::PendingToggles.store(0);
            TryResetRenderStateForUnload();
            RuntimeLock.unlock();
            return oPostRender(rcx, canvas);
        }
        g_Hack::TickNativeFreeCamera();
        if (PhoenixShortcuts::UpdateGameplay(g_Util::GetLocalPC(),
            g_MDX12::g_MenuState::g_isOpen, g_Hack::IsFreeCameraActive())) {
            g_Hack::CancelNoteXPBuff();
            g_TekSpear::RequestReset();
        }
        g_Hack::BackpackToolsTick(SDK::UWorld::GetWorld());
        g_Hack::AutoDepositItems(SDK::UWorld::GetWorld());
        g_Hack::UpdateSessionSnapshot(SDK::UWorld::GetWorld());
        if (!UpdateCameraOK) {
            g_Hack::AutoHarvestResources(SDK::UWorld::GetWorld());
            g_Hack::WorldUtilitiesTick(SDK::UWorld::GetWorld());
            g_Hack::GameplayExtensionsTick(SDK::UWorld::GetWorld());
            g_Hack::AutoOpenGachaCrystals(SDK::UWorld::GetWorld());
            g_Hack::AutoCraft(SDK::UWorld::GetWorld());
            if (!g_Hack::IsFreeCameraActive()) {
                g_TekSpear::Tick();
                g_Aimbot::Tick();
            }
        }
        g_MDX12::SetupPostRender(rcx, canvas);

		// Keep the viewport post-processing show flag enabled. Component stencil
		// writes now run from UpdateCamera, before the scene is rendered.
		g_DrawESP::MaintainResourceModelHighlightState();

        RuntimeLock.unlock();
        oPostRender(rcx, canvas);
        RuntimeLock.lock();
        // The request may have arrived after the entry check. Restore on this
        // same game-thread callback instead of waiting for another frame.
        if (ShouldBypassFeatures()) {
            TryResetRenderStateForUnload();
        }
        return;

        /*
        if (rcx && canvas) {
            g_MDX12::SetupPostRender(rcx, canvas);
        }
        */
    }

    void __fastcall hkUpdateCamera(
        SDK::AShooterPlayerCameraManager* CameraManager, float DeltaTime) {
        CallbackScope Guard;
        thread_local bool IsInsideUpdateCamera = false;
        if (IsInsideUpdateCamera) {
            return oUpdateCamera(CameraManager, DeltaTime);
        }
        struct ScopedUpdateCameraCall {
            bool& Flag;
            explicit ScopedUpdateCameraCall(bool& InFlag) : Flag(InFlag) { Flag = true; }
            ~ScopedUpdateCameraCall() { Flag = false; }
        } CallGuard(IsInsideUpdateCamera);

        std::unique_lock<std::recursive_mutex> RuntimeLock(g_GameRuntimeMutex);

        if (ShouldBypassFeatures()) {
            TryResetRenderStateForUnload();
            RuntimeLock.unlock();
            return oUpdateCamera(CameraManager, DeltaTime);
        }

        g_Hack::PrepareFreeCameraUpdate(CameraManager);
        if (CameraManager && CameraManager->PCOwner == g_Util::GetLocalPC()) {
            g_Hack::AutoHarvestResources(SDK::UWorld::GetWorld());
            g_Hack::WorldUtilitiesTick(SDK::UWorld::GetWorld());
            g_Hack::GameplayExtensionsTick(SDK::UWorld::GetWorld());
            g_Hack::AutoOpenGachaCrystals(SDK::UWorld::GetWorld());
            g_Hack::AutoCraft(SDK::UWorld::GetWorld());
        }
        if (!g_Hack::IsFreeCameraActive()) g_Hack::MaintainAutoHarvestCameraAim(CameraManager);
        const bool HighlightPrepared =
            g_DrawESP::PrepareModelHighlightCameraUpdate(CameraManager);
        RuntimeLock.unlock();
        oUpdateCamera(CameraManager, DeltaTime);
        RuntimeLock.lock();

        // An unload request can arrive while either the native update or our
        // finish step is running. Restore in this callback and never wait for a
        // later frame with a temporary blendable still installed.
        if (ShouldBypassFeatures()) {
            if (HighlightPrepared) {
                g_DrawESP::FinishModelHighlightCameraUpdate(CameraManager);
            }
            TryResetRenderStateForUnload();
            return;
        }
        if (HighlightPrepared) {
            g_DrawESP::FinishModelHighlightCameraUpdate(CameraManager);
        }
        g_Hack::MaintainMountedFullRotationCamera(CameraManager);
        g_Hack::UpdateFreeCamera(CameraManager, DeltaTime);
        // Selection uses the finished camera view. Tek spear may temporarily
        // turn for a parry; the weapon-aim redirect itself keeps the view intact.
        if (!ShouldBypassFeatures() &&
            CameraManager && CameraManager->PCOwner == g_Util::GetLocalPC()) {
            if (!g_Hack::IsFreeCameraActive()) {
                g_TekSpear::Tick();
                g_Aimbot::Tick();
            }
        }
        if (ShouldBypassFeatures()) {
            TryResetRenderStateForUnload();
        }
    }

    void __fastcall hkPhysicsRotation(SDK::UMovementComponent* rcx, float DeltaTime)
    {
        CallbackScope Guard;
        if (!ShouldBypassFeatures()) g_MDX12::SetupPhysicsRotation(rcx, DeltaTime);

        oPhysicsRotation(rcx, DeltaTime);
        if (!ShouldBypassFeatures()) g_Hack::NativeMountPhysicsHook(rcx);
    }

    /*
    float __fastcall hkTakeDamage(
        SDK::AActor* _this,
        float DamageAmount,
        SDK::FDamageEvent* DamageEvent,
        SDK::AController* Instigator,
        SDK::AActor* DamageCauser)
    {
        g_MDX12::SetupTakeDamage(_this, DamageAmount, DamageEvent, Instigator, DamageCauser);

        return oTakeDamage(_this, DamageAmount, DamageEvent, Instigator, DamageCauser);
    }
    */

    void __fastcall hkClientNotifyReconnected(SDK::APrimalPlayerController* rcx, SDK::APawn* NewPawn) {
        CallbackScope Guard;
        if (!ShouldBypassFeatures() && g_Config::bFastReconnected) {
            return;
        }

        return oClientNotifyReconnected(rcx, NewPawn);
    }

    void __fastcall hkClientNotifyRespawned(SDK::APrimalPlayerController* rcx, SDK::APawn* NewPawn, bool IsFirstSpawn) {
        CallbackScope Guard;
        PhoenixRespawn::Dispatch(rcx, NewPawn, IsFirstSpawn,
            g_Config::bFastRespawned, ShouldBypassFeatures(), oClientNotifyRespawned);
    }

    void __fastcall hkClientChatMessage(SDK::AShooterPlayerController* rcx, SDK::FPrimalChatMessage& Chat) {
        CallbackScope Guard;
        // g_LogManager::AddLog(255, 255, 255, 255, "hkClientChatMessage");

        if (!ShouldBypassFeatures() && g_Config::bBlockedChat) {
            // g_LogManager::AddLog(255, 255, 255, 255, "bBlockedChat");
            return; // 可以阻止客户端绘制聊天消息，无法阻止服务器RPC
        }

        oClientChatMessage(rcx, Chat);
    }

    void __fastcall hkClientAddFloatingDamageText(
        SDK::AShooterPlayerController* _this,
        SDK::FVector_NetQuantize& AtLocation,
        SDK::int32 DamageAmount,
        SDK::int32 FromTeamID)
    {
        CallbackScope Guard;
        // g_LogManager::AddLog(255, 0, 0, 255, std::format("{} {}", _this->GetPlayerCharacterName().ToString(), DamageAmount));
        oClientAddFloatingDamageText(_this, AtLocation, DamageAmount, FromTeamID);
    }

    // 2026/3/29 @zetsr
    // 不再需要这种方法了，因为UWorld的虚表无法在游戏更新后稳定
    /*
    void initUWorldTick() {
        SDK::UWorld* pWorld = nullptr;
        while (!pWorld) {
            pWorld = SDK::UWorld::GetWorld();
            if (pWorld && pWorld->OwningGameInstance) break;
            Sleep(1);
        }

        void** vtable = *reinterpret_cast<void***>(pWorld);
        void* pTarget = vtable[336]; // 220

        if (pTarget) {
            if (MH_CreateHook(pTarget, &g_Hook::hkUWorldTick, reinterpret_cast<LPVOID*>(&g_Hook::oWorldTick)) == MH_OK) {
                // MH_EnableHook(pTarget);
            }
        }
    }
    */

    void initActorTick() {
        /*
        std::string pattern = g_CheatData::Signature::AActor::Tick;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, &hkActorTick, reinterpret_cast<LPVOID*>(&oActorTick)) == MH_OK) {
                MH_EnableHook(targetAddr);
                ActorTickOK = true;
            }
        }
        */

        g_Util::ADD_MH(g_CheatData::Signature::AActor::Tick, &ActorTickOK, &hkActorTick, &oActorTick);
    }

    void initHandleDisconnect() {
        /*
        std::string pattern = g_CheatData::Signature::UNetConnection::HandleDisconnect;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, &hkHandleDisconnect, reinterpret_cast<LPVOID*>(&oHandleDisconnect)) == MH_OK) {
                MH_EnableHook(targetAddr);
				HandleDisconnectOK = true;
            }
        }
        */

        g_Util::ADD_MH(g_CheatData::Signature::UNetConnection::HandleDisconnect, &HandleDisconnectOK, &hkHandleDisconnect, &oHandleDisconnect);
    }

    void initOutputTextLine() {
        /*
        std::string pattern = g_CheatData::Signature::UConsole::OutputTextLine;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, &hkOutputTextLine, reinterpret_cast<LPVOID*>(&oOutputTextLine)) == MH_OK) {
                MH_EnableHook(targetAddr);
                OutputTextLineOK = true;
            }
        }
        */

        SDK::UEngine* pEngine = SDK::UEngine::GetEngine();
        SDK::UConsole* ViewportConsole = nullptr;

        const auto Started = GetTickCount64();
        while (!g_UnloadRequested.load(std::memory_order_acquire) && GetTickCount64() - Started < 3000) {
            pEngine = SDK::UEngine::GetEngine();
            if (pEngine && pEngine->GameViewport && pEngine->GameViewport->ViewportConsole) {
                ViewportConsole = pEngine->GameViewport->ViewportConsole;
                break;
            }

            Sleep(100);
        }

        if (ViewportConsole) {
            auto** VTable = *reinterpret_cast<void***>(ViewportConsole);
            const auto Slot = g_CheatData::VTable::UConsole::OutputTextLine;
            if (ezVMT::Accessible(VTable + Slot, sizeof(void*)) && IsExecutableGameAddress(VTable[Slot]))
                OutputTextLineOK = ezVMT::CreateHook((void*)&hkOutputTextLine, ViewportConsole, Slot, (void**)&oOutputTextLine) &&
                    ezVMT::EnableHook((void*)&hkOutputTextLine);
            if (!OutputTextLineOK) g_Hook::LogLifecycle("Console hook skipped: native VMT ownership could not be verified.");
        }
    }

    void initPostRender() {
        /*
        std::string pattern = g_CheatData::Signature::UGameViewportClient::PostRender;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, &hkPostRender, reinterpret_cast<LPVOID*>(&oPostRender)) == MH_OK) {
                MH_EnableHook(targetAddr);
                PostRenderOK = true;
            }
        }
        */

        SDK::UEngine* pEngine = SDK::UEngine::GetEngine();
        SDK::UGameViewportClient* GameViewport = nullptr;

        const auto Started = GetTickCount64();
        while (!g_UnloadRequested.load(std::memory_order_acquire) && GetTickCount64() - Started < 10000) {
            pEngine = SDK::UEngine::GetEngine();
            if (pEngine && pEngine->GameViewport) {
                GameViewport = pEngine->GameViewport;
                break;
            }

            Sleep(100);
        }

        if (GameViewport) {
            auto** VTable = *reinterpret_cast<void***>(GameViewport);
            const auto Slot = g_CheatData::VTable::UGameViewportClient::PostRender;
            if (ezVMT::Accessible(VTable + Slot, sizeof(void*)) && IsExecutableGameAddress(VTable[Slot]))
                PostRenderOK = ezVMT::CreateHook((void*)&hkPostRender, GameViewport, Slot, (void**)&oPostRender) &&
                    ezVMT::EnableHook((void*)&hkPostRender);
            if (!PostRenderOK) g_Hook::LogLifecycle("Viewport hook skipped: native VMT ownership could not be verified.");
        }
    }

    void initUpdateCamera() {
        SDK::AShooterPlayerCameraManager* CameraManagerCDO =
            SDK::AShooterPlayerCameraManager::GetDefaultObj();
        if (!CameraManagerCDO) return;

        void** VTable = *reinterpret_cast<void***>(CameraManagerCDO);
        if (!VTable) return;
        // Client and dedicated-server builds do not have identical camera
        // vtables. Search a narrow window and require a unique, semantically
        // verified UpdateCamera(float) prologue before installing a detour.
        void* TargetAddress = nullptr;
        int MatchCount = 0;
        constexpr int FirstCandidateSlot = 468;
        constexpr int LastCandidateSlot = 476;
        for (int Slot = FirstCandidateSlot; Slot <= LastCandidateSlot; ++Slot) {
            void* Candidate = VTable[Slot];
            if (!IsExpectedUpdateCameraAddress(Candidate)) continue;
            TargetAddress = Candidate;
            ++MatchCount;
        }
        if (MatchCount != 1 || !TargetAddress) return;

        const MH_STATUS CreateStatus = MH_CreateHook(TargetAddress,
            reinterpret_cast<void*>(&hkUpdateCamera),
            reinterpret_cast<void**>(&oUpdateCamera));
        if (CreateStatus != MH_OK) return;

        const MH_STATUS EnableStatus = MH_EnableHook(TargetAddress);
        if (EnableStatus == MH_OK || EnableStatus == MH_ERROR_ENABLED) {
            UpdateCameraOK = true;
            return;
        }

        MH_RemoveHook(TargetAddress);
        oUpdateCamera = nullptr;
    }

    void initPhysicsRotation() {
        PhysicsRotationOK = false;
        auto* Default = SDK::UCharacterMovementComponent::GetDefaultObj();
        if (!Default) return;
        std::string pattern = g_CheatData::Signature::UMovementComponent::PhysicsRotation;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];
            // A matching byte pattern alone previously selected velocity
            // braking (three floats). Require the verified rotation slot too.
            if (targetAddr != (*reinterpret_cast<void***>(Default))[g_CheatData::VTable::UMovementComponent::PhysicsRotation] ||
                !IsExecutableGameAddress(targetAddr)) return;

            if (MH_CreateHook(targetAddr, &hkPhysicsRotation, reinterpret_cast<LPVOID*>(&oPhysicsRotation)) == MH_OK) {
                if (MH_EnableHook(targetAddr) == MH_OK) {
                    PhysicsRotationOK = true;
                } else {
                    MH_RemoveHook(targetAddr);
                    oPhysicsRotation = nullptr;
                }
            }
        }
    }

    /*
    void initTakeDamage() {
        g_Util::ADD_MH(g_CheatData::Signature::AActor::TakeDamage, &TakeDamageOK, &hkTakeDamage, &oTakeDamage);
    }
    */

    void initClientNotifyReconnected() {
        /*
        std::string pattern = g_CheatData::Signature::APrimalPlayerController::ClientNotifyReconnected;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, &hkClientNotifyReconnected, reinterpret_cast<LPVOID*>(&oClientNotifyReconnected)) == MH_OK) {
                MH_EnableHook(targetAddr);
                ClientNotifyReconnectedOK = true;
            }
        }
        */
    }

    void initClientNotifyRespawned() {
        ClientNotifyRespawnedOK = false;
        const auto* Wrapper = static_cast<const uint8_t*>(g_Util::FindUFunction(
            "Function Engine.PrimalPlayerController.ClientNotifyRespawned"));
        constexpr size_t WrapperBytes = 0x17b;
        if (!IsExecutableGameAddress(const_cast<uint8_t*>(Wrapper)) ||
            !ezVMT::Accessible(Wrapper, WrapperBytes)) return;
        const auto Slot = PhoenixRespawn::NativeSlot({Wrapper, WrapperBytes});
        auto* Default = SDK::AShooterPlayerController::GetDefaultObj();
        if (!Slot || !Default) {
            g_Hook::LogLifecycle("Respawn hook skipped: native dispatch layout is unsupported.");
            return;
        }
        auto** VTable = *reinterpret_cast<void***>(Default);
        if (!VTable || !ezVMT::Accessible(VTable + *Slot, sizeof(void*)) ||
            !IsExecutableGameAddress(VTable[*Slot])) return;
        // UE's exec wrapper retains argument parsing and stack advancement.
        // This target accepts the actual NewPawn and IsFirstSpawn arguments.
        void* Target = VTable[*Slot];
        if (MH_CreateHook(Target, reinterpret_cast<void*>(hkClientNotifyRespawned),
            reinterpret_cast<void**>(&oClientNotifyRespawned)) != MH_OK) return;
        if (MH_EnableHook(Target) == MH_OK) ClientNotifyRespawnedOK = true;
        else {
            MH_RemoveHook(Target);
            oClientNotifyRespawned = nullptr;
        }
    }

    void initClientChatMessage() {
        /*
        std::string pattern = g_CheatData::Signature::AShooterPlayerController::ClientChatMessage;
        AOB::Result ok = AOB::Scan(pattern);

        if (ok && ok.size() == 1) {
            void* targetAddr = ok[0];

            if (MH_CreateHook(targetAddr, &hkClientChatMessage, reinterpret_cast<LPVOID*>(&oClientChatMessage)) == MH_OK) {
                MH_EnableHook(targetAddr);
                ClientChatMessageOK = true;
            }
        }
        */
    }

    void initClientAddFloatingDamageText() {
        void* targetAddr = g_Util::FindUFunction("Function ShooterGame.ShooterPlayerController.ClientAddFloatingDamageText");
        g_Util::ADD_MH(targetAddr, &ClientAddFloatingDamageTextOK, &hkClientAddFloatingDamageText, &oClientAddFloatingDamageText);
    }
}

// 应该不需要每个都单独检查一次
void g_Hook::StartAllHooks() {
    SDK::UWorld* pWorld = nullptr;
    while (!g_UnloadRequested.load(std::memory_order_acquire)) {
        pWorld = SDK::UWorld::GetWorld();
        if (pWorld && pWorld->OwningGameInstance) break;
        Sleep(10);
    }

    if (pWorld && !g_UnloadRequested.load(std::memory_order_acquire)) {
        // 理论上来说如果用户在UWorld非常不稳定的时候注入有极低概率发生检查后使用，但是这应该不是问题，因为如果环境不稳定，d3d12 hook会先崩溃
        g_Hook::initActorTick();
        g_Hook::initHandleDisconnect();
        g_Hook::initOutputTextLine();
        g_Hook::initPostRender();
        g_Hook::initUpdateCamera();
        g_Hook::initWeaponAim();
        g_Hook::initProjectileShotEvents();
        g_Hook::initDinoActionEvents();
        g_Hook::initProjectileModifiers();
        g_Hook::initTekBowInstantAim();
        g_Hook::initShotgunSpread();
        g_Hook::initInstantThrow();
        g_Hook::initBowFullChargeShot();
        g_Hook::initPhysicsRotation();
        // g_Hook::initTakeDamage();
        g_Hook::initClientNotifyReconnected();
        g_Hook::initClientNotifyRespawned();
        g_Hook::initClientChatMessage();
        g_Hook::initClientAddFloatingDamageText();
    }
}

bool g_Hook::StopAllHooks() {
    g_MDX12::g_MenuState::g_isOpen = false;
    g_MDX12::g_InputState::g_blockMouseInput = false;
    g_MDX12::g_InputState::g_blockKeyboardInput = false;

    const MH_STATUS DisableStatus = MH_DisableHook(MH_ALL_HOOKS);
    const bool MinHookStopped = DisableStatus == MH_OK || DisableStatus == MH_ERROR_NOT_INITIALIZED;
    const bool VmtStopped = ezVMT::RemoveAllHooks();
    const bool WindowStopped = g_MDX12::inputhook::RemoveAll();

    ActorTickOK = false;
    HandleDisconnectOK = false;
    OutputTextLineOK = false;
    PostRenderOK = false;
    UpdateCameraOK = false;
    WeaponAimOK = false;
    TekBowInstantAimOK = false;
    ShotgunSpreadOK = false;
    InstantThrowOK = false;
    BowFullChargeOK = false;
    oStartFire = nullptr;
    PhysicsRotationOK = false;
    ClientNotifyReconnectedOK = false;
    ClientNotifyRespawnedOK = false;
    ClientChatMessageOK = false;
    ClientAddFloatingDamageTextOK = false;
    ProjectileShotEventsOK = false;
    DinoActionEventsOK = false;
    DinoActionEventTarget = nullptr;
    ProjectileModifiersOK = false;
    oProjectileSpeedModifier = nullptr;
    oProjectileGravityModifier = nullptr;
    return MinHookStopped && VmtStopped && WindowStopped;
}

void g_Hook::SetModuleHandle(HMODULE module) {
    g_ModuleHandle.store(module, std::memory_order_release);
}
void g_Hook::SetInitializationThread(HANDLE Thread) { g_InitializationThread.store(Thread, std::memory_order_release); }
void g_Hook::SetRuntimeReady(bool Ready) { g_RuntimeReady.store(Ready, std::memory_order_release); }
bool g_Hook::IsStopping() { return ShouldBypassFeatures(); }
bool g_Hook::IsUnloadRequested() { return g_UnloadRequested.load(std::memory_order_acquire); }

void g_Hook::RequestUnload() {
    // Serialize the short publication sequence. Reset ownership must be clear
    // before game-thread callbacks can observe the unload request.
    if (g_UnloadRequestGate.test_and_set(std::memory_order_acquire)) return;
    if (g_UnloadWorkerActive.exchange(true, std::memory_order_acq_rel)) {
        g_UnloadRequestGate.clear(std::memory_order_release);
        return;
    }

    HMODULE module = g_ModuleHandle.load(std::memory_order_acquire);
    if (!module) {
        g_UnloadWorkerActive.store(false, std::memory_order_release);
        g_UnloadRequestGate.clear(std::memory_order_release);
        return;
    }

    g_RenderStateResetStarted.store(false, std::memory_order_relaxed);
    g_RenderStateResetComplete.store(false, std::memory_order_relaxed);
    g_UnloadRequested.store(true, std::memory_order_release);
    HANDLE thread = CreateThread(nullptr, 0, UnloadWorker, module, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
    else {
        g_UnloadWorkerActive.store(false, std::memory_order_release);
        g_UnloadRequested.store(false, std::memory_order_release);
        g_RenderStateResetStarted.store(false, std::memory_order_release);
        g_RenderStateResetComplete.store(false, std::memory_order_release);
    }
    g_UnloadRequestGate.clear(std::memory_order_release);
}

namespace {
    DWORD WINAPI UnloadWorker(LPVOID parameter) {
        HMODULE module = static_cast<HMODULE>(parameter);
        struct WorkerState { ~WorkerState() { g_UnloadWorkerActive.store(false, std::memory_order_release); } } Worker;
        g_Hook::LogLifecycle("Unload requested: waiting for initialization thread.");
        HANDLE Initialization = g_InitializationThread.load(std::memory_order_acquire);
        if (Initialization && WaitForSingleObject(Initialization, 5000) != WAIT_OBJECT_0) {
            g_Hook::LogLifecycle("Unload blocked: initialization is still active; DLL remains resident."); return 1;
        }
        if (Initialization) { CloseHandle(Initialization); g_InitializationThread.store(nullptr, std::memory_order_release); }
        if (!g_RuntimeReady.load(std::memory_order_acquire)) g_RenderStateResetComplete.store(true, std::memory_order_release);
        g_Hook::LogLifecycle("Unload: waiting for game-thread state restoration.");

        // Give the next UpdateCamera/PostRender game-thread callback a bounded
        // window to restore model/camera state. Never perform UObject/TArray
        // restoration from this worker thread.
        for (int WaitedMilliseconds = 0;
            WaitedMilliseconds < 3000 &&
            !g_RenderStateResetComplete.load(std::memory_order_acquire);
            WaitedMilliseconds += 10) {
            Sleep(10);
        }

        if (!g_RenderStateResetComplete.load(std::memory_order_acquire)) {
            // A loading screen or closed viewport can temporarily provide no
            // game-thread camera/render callback. Keep the DLL and hooks alive
            // instead of unloading with owned CustomDepth/input state still in
            // the game. The user can retry unload once gameplay resumes.
            g_UnloadRequested.store(false, std::memory_order_release);
            g_RenderStateResetStarted.store(false, std::memory_order_release);
            g_Hook::LogLifecycle("Unload postponed: no game-thread restoration acknowledgement; DLL remains resident.");
            return 0;
        }

        // Prevent new callbacks, then wait until every in-flight detour has
        // left this image before the MinHook trampolines are destroyed.
        // Drain callbacks that entered before the stop flag, so none can
        // initialize input hooks or call MH_EnableHook after detachment.
        if (!PhoenixUnload::WaitForQuiet(g_ActiveHookCalls, 5000, 0)) {
            g_Hook::LogLifecycle("Unload blocked: callbacks did not quiesce before detachment; DLL remains resident."); return 2;
        }
        g_Hook::LogLifecycle("Unload: restoring WndProc/VMTs and disabling all MinHook detours.");
        if (!g_Hook::StopAllHooks()) {
            g_Hook::LogLifecycle("Unload blocked: a detour, VMT or window subclass could not be detached; DLL remains resident."); return 3;
        }
        if (!PhoenixUnload::WaitForQuiet(g_ActiveHookCalls, 5000)) {
            g_Hook::LogLifecycle("Unload blocked: callbacks remain in flight after detachment; DLL remains resident."); return 4;
        }

        // Detached downloader threads previously kept executing code from this
        // module. They are now cancellable and joinable, so stop them first.
        LanguageManager::Get().Shutdown();
        LuaManager::Get().Shutdown();
        g_Hook::LogLifecycle("Unload: worker threads joined; checking callback return paths.");
        if (!PhoenixUnload::WaitForImageExit(module, g_ActiveHookCalls, 5000)) {
            g_Hook::LogLifecycle("Unload blocked: a thread still references this DLL's code; DLL remains resident."); return 5;
        }
        if (!g_MDX12::FinalCleanupAll()) {
            g_Hook::LogLifecycle("Unload blocked: overlay GPU work did not finish; resources and DLL retained."); return 6;
        }
        const auto MinHookStatus = MH_Uninitialize();
        if (MinHookStatus != MH_OK && MinHookStatus != MH_ERROR_NOT_INITIALIZED) {
            g_Hook::LogLifecycle("Unload blocked: MinHook could not release detours safely; DLL remains resident."); return 7;
        }
        g_Hook::LogLifecycle("Unload complete: callbacks drained, native hooks restored, GPU resources released. Releasing DLL.");
        g_Hook::CloseLifecycleLog();
        FreeLibraryAndExitThread(module, 0);
        return 0;
    }
}

namespace g_MDX12 {
    static DXGI_FORMAT g_LastRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    using PFN_ResourceBarrier = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
    using PFN_EnhancedBarrier = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*, UINT32, const D3D12_BARRIER_GROUP*);
    using PFN_CommandListReset = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
    static PFN_ResourceBarrier oResourceBarrier = nullptr;
    static PFN_EnhancedBarrier oEnhancedBarrier = nullptr;
    static PFN_CommandListReset oCommandListReset = nullptr;

    void STDMETHODCALLTYPE hkEnhancedBarrier(ID3D12GraphicsCommandList7* List, UINT32 Count, const D3D12_BARRIER_GROUP* Groups) {
        g_Hook::CallbackScope Guard;
        oEnhancedBarrier(List, Count, Groups);
        if (!ShouldBypassFeatures()) g_QueueTracker.OnEnhancedBarriers(List, Count, Groups);
    }
    void STDMETHODCALLTYPE hkResourceBarrier(ID3D12GraphicsCommandList* List, UINT Count, const D3D12_RESOURCE_BARRIER* Barriers) {
        g_Hook::CallbackScope Guard;
        oResourceBarrier(List, Count, Barriers);
        if (!ShouldBypassFeatures()) g_QueueTracker.OnResourceBarriers(List, Count, Barriers);
    }
    HRESULT STDMETHODCALLTYPE hkCommandListReset(ID3D12GraphicsCommandList* List, ID3D12CommandAllocator* Allocator, ID3D12PipelineState* State) {
        g_Hook::CallbackScope Guard;
        const HRESULT Result = oCommandListReset(List, Allocator, State);
        if (SUCCEEDED(Result) && !ShouldBypassFeatures()) g_QueueTracker.OnReset(List);
        return Result;
    }
    void STDMETHODCALLTYPE hkExecuteCommandLists(ID3D12CommandQueue* Queue, UINT Count, ID3D12CommandList* const* Lists) {
        g_Hook::CallbackScope Guard;
        if (ShouldBypassFeatures()) {
            if (g_HookFunctions::g_oExecuteCommandLists) g_HookFunctions::g_oExecuteCommandLists(Queue, Count, Lists);
            return;
        }
        g_QueueTracker.ObserveExecution(Queue);
        auto Submission = g_QueueTracker.CaptureSubmission(Count, Lists);
        if (g_HookFunctions::g_oExecuteCommandLists) g_HookFunctions::g_oExecuteCommandLists(Queue, Count, Lists);
        if (!ShouldBypassFeatures()) g_QueueTracker.CompleteSubmission(Queue, Submission);
    }
    #include "PresentationHooks.inl"
    HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        g_Hook::CallbackScope Guard;
        if (!ShouldBypassFeatures()) return ResizeOverlayBuffers(SwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        return g_HookFunctions::g_oResizeBuffers ? g_HookFunctions::g_oResizeBuffers(SwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags) : S_OK;
    }

    DWORD WINAPI MainThread(LPVOID lpParam) {
        if (MH_Initialize() != MH_OK) return 0;

        // 等待目标进程自然加载 d3d12.dll 与 dxgi.dll
        if (!g_RuntimeModules::WaitAndLoad()) {
            return 0;
        }

        g_Hook::StartAllHooks();
        if (!g_Hook::PostRenderOK && !g_Hook::UpdateCameraOK) return 0;
        // g_Hook::initUWorldTick();
        // g_Hook::initHandleDisconnect();
        // g_Hook::initOutputTextLine();
        // g_Hook::initPostRender();

        // 此时 d3d12.dll 与 dxgi.dll 已被目标进程加载，可以安全操作
        const auto DeviceWaitStarted = GetTickCount64();
        while (!g_Hook::IsUnloadRequested() && GetTickCount64() - DeviceWaitStarted < 30000) {
            WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"TempDX12", nullptr };
            RegisterClassEx(&wc);
            HWND tempWnd = CreateWindow(wc.lpszClassName, L"Temp", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
            if (!tempWnd) { UnregisterClass(wc.lpszClassName, wc.hInstance); Sleep(1); continue; }

            ID3D12Device* tempDevice = nullptr;
            ID3D12CommandQueue* tempQueue = nullptr;
            ID3D12CommandAllocator* tempAllocator = nullptr;
            ID3D12GraphicsCommandList* tempList = nullptr;
            ID3D12GraphicsCommandList7* tempList7 = nullptr;
            IDXGIFactory4* factory = nullptr;
            IDXGISwapChain* tempSwapChain = nullptr;
            IDXGISwapChain1* tempSwapChain1 = nullptr;

            // 使用运行时加载的函数指针，而非编译期链接的符号
            bool ok = SUCCEEDED(g_RuntimeModules::g_pD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tempDevice)));

            if (ok) {
                D3D12_COMMAND_QUEUE_DESC qdesc = {};
                qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                qdesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
                qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                qdesc.NodeMask = 0;
                ok = SUCCEEDED(tempDevice->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&tempQueue)));
            }

            if (ok) ok = SUCCEEDED(tempDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAllocator)));
            if (ok) ok = SUCCEEDED(tempDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAllocator, nullptr, IID_PPV_ARGS(&tempList)));
            if (ok) ok = SUCCEEDED(tempList->Close());
            // Enhanced barriers are optional on older Windows/D3D12 runtimes.
            // When command-list 7 is available, its Barrier slot must be hooked
            // alongside the legacy ResourceBarrier slot.
            if (ok) tempList->QueryInterface(IID_PPV_ARGS(&tempList7));
            if (ok) {
                // 使用运行时加载的函数指针
                ok = SUCCEEDED(g_RuntimeModules::g_pCreateDXGIFactory1(IID_PPV_ARGS(&factory)));
            }

            if (ok) {
                DXGI_SWAP_CHAIN_DESC sd = {};
                sd.BufferCount = 2;
                sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                sd.OutputWindow = tempWnd;
                sd.SampleDesc.Count = 1;
                sd.Windowed = TRUE;
                sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                ok = SUCCEEDED(factory->CreateSwapChain(tempQueue, &sd, &tempSwapChain));
            }

            if (ok) {
                void** swapVTable = *reinterpret_cast<void***>(tempSwapChain);
                void** queueVTable = *reinterpret_cast<void***>(tempQueue);
                void** listVTable = *reinterpret_cast<void***>(tempList);
                void** list7VTable = tempList7 ? *reinterpret_cast<void***>(tempList7) : nullptr;

                bool PresentReady = false, Present1Ready = false, ResizeReady = false, QueueReady = false;
                if (SUCCEEDED(tempSwapChain->QueryInterface(IID_PPV_ARGS(&tempSwapChain1)))) {
                    void** Table1 = *reinterpret_cast<void***>(tempSwapChain1);
                    // IDXGISwapChain1::Present1 is slot 22 (public DXGI ABI).
                    LogPresentationTarget("Present1", Table1[22]);
                    Present1Ready = MH_CreateHook(Table1[22], reinterpret_cast<LPVOID>(hkPresent1), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oPresent1)) == MH_OK;
                }
                if (swapVTable && swapVTable[g_CheatData::VTable::D3D12::Present]) {
                    LogPresentationTarget("Present", swapVTable[g_CheatData::VTable::D3D12::Present]);
                    PresentReady = MH_CreateHook(swapVTable[g_CheatData::VTable::D3D12::Present], reinterpret_cast<LPVOID>(hkPresent), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oPresent)) == MH_OK;
                }

                if (swapVTable && swapVTable[g_CheatData::VTable::D3D12::ResizeBuffers]) {
                    ResizeReady = MH_CreateHook(swapVTable[g_CheatData::VTable::D3D12::ResizeBuffers], reinterpret_cast<LPVOID>(hkResizeBuffers), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oResizeBuffers)) == MH_OK;
                }

                if (queueVTable && queueVTable[g_CheatData::VTable::D3D12::ExecuteCommandLists]) {
                    LogPresentationTarget("ExecuteCommandLists", queueVTable[g_CheatData::VTable::D3D12::ExecuteCommandLists]);
                    QueueReady = MH_CreateHook(queueVTable[g_CheatData::VTable::D3D12::ExecuteCommandLists], reinterpret_cast<LPVOID>(hkExecuteCommandLists), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oExecuteCommandLists)) == MH_OK;
                }

                // Public Windows SDK vtable slots: Reset=10,
                // ResourceBarrier=26, ID3D12GraphicsCommandList7::Barrier=80.
                // Observe both legacy and enhanced native recordings to
                // associate each exact backbuffer with its submitting queue.
                const bool BarrierReady = MH_CreateHook(listVTable[26], reinterpret_cast<LPVOID>(hkResourceBarrier), reinterpret_cast<LPVOID*>(&oResourceBarrier)) == MH_OK;
                const bool EnhancedBarrierReady = !list7VTable ||
                    (list7VTable[80] && MH_CreateHook(list7VTable[80], reinterpret_cast<LPVOID>(hkEnhancedBarrier), reinterpret_cast<LPVOID*>(&oEnhancedBarrier)) == MH_OK);
                const bool ResetReady = MH_CreateHook(listVTable[10], reinterpret_cast<LPVOID>(hkCommandListReset), reinterpret_cast<LPVOID*>(&oCommandListReset)) == MH_OK;
                const bool HookReady = PresentReady && Present1Ready && ResizeReady && QueueReady && BarrierReady && EnhancedBarrierReady && ResetReady &&
                    !g_Hook::IsUnloadRequested() && MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
                if (tempSwapChain1) tempSwapChain1->Release();
                tempSwapChain->Release();
                factory->Release();
                tempQueue->Release();
                if (tempList7) tempList7->Release();
                tempList->Release();
                tempAllocator->Release();
                tempDevice->Release();
                DestroyWindow(tempWnd);
                UnregisterClass(wc.lpszClassName, wc.hInstance);

                if (!HookReady) g_Hook::LogLifecycle("D3D12 initialization failed: required overlay detours were not installed/enabled; coordinating rollback.");
                else g_Hook::LogLifecycle(list7VTable ?
                    "D3D12 hooks installed: Present, Present1, ExecuteCommandLists, legacy ResourceBarrier, and enhanced Barrier." :
                    "D3D12 hooks installed: Present, Present1, ExecuteCommandLists, and legacy ResourceBarrier; enhanced Barrier unsupported by this runtime.");
                return HookReady ? 1 : 0;
            }

            if (tempSwapChain1) tempSwapChain1->Release();
            if (tempSwapChain) tempSwapChain->Release();
            if (factory) factory->Release();
            if (tempQueue) tempQueue->Release();
            if (tempList7) tempList7->Release();
            if (tempList) tempList->Release();
            if (tempAllocator) tempAllocator->Release();
            if (tempDevice) tempDevice->Release();

            DestroyWindow(tempWnd);
            UnregisterClass(wc.lpszClassName, wc.hInstance);

            // 2026/3/29 @zetsr
            // 才发现不知道啥时候初始化了两次，初始化的时候居然一直没崩溃，minhook确实厉害
            // 好吧，它其实正在循环初始化，简直是狗屎
            // g_Hook::initUWorldTick();

            Sleep(1);
        }

        g_Hook::LogLifecycle("D3D12 initialization stopped: cancellation or device setup timeout.");
        return 0;
    }
}
