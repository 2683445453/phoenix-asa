// 秒换弹 / 弓秒蓄力规则 + 字段补丁回归（不注入游戏）。
//
// 纯规则部分（InstantReloadRules.h）不依赖 SDK；
// 补丁器部分（InstantReloadPatch.h）使用真实 CppSDK 类型，与
// tests/feature_expansion_patch.cpp 一样只用自建对象/属性描述符驱动，
// 不需要运行中的游戏进程。
#include "../src/internal/Hack/InstantReloadRules.h"
#include "../src/internal/Hack/InstantReloadPatch.h"
#include "../src/internal/Config/ConfigManager.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace PhoenixReload;
namespace fs = std::filesystem;

// 补丁器使用真实 CppSDK 类型，因此需要这两个 SDK 静态/成员函数的最小离线实现
// （对应 Basic.cpp / CoreUObject_functions.cpp；测试不依赖其真实取值）。
namespace SDK {
    uintptr_t InSDKUtils::GetImageBase() { return 0; }
    bool UObject::IsDefaultObject() const {
        return (Flags & EObjectFlags::ClassDefaultObject) != 0;
    }
}

// 运行时身份串来自 NormalizedClassChain：类链 + 关联物品类链，全部小写并去掉
// 非字母数字字符，因此这里用等价形态构造测试输入。
static const char* PumpShotgun =
    "weapmachinedshotgun c shoot erweapon projectile c shoot erweapon c actor c "
    "primalitem weapon machinedshotgun c primalitemweapongenericc";
static const char* AssaultRifle =
    "weaprifle c shoot erweapon instant c shoot erweapon c actor c "
    "primalitem weapon rifle c primalitemweapongenericgun c";
static const char* Crossbow =
    "weapcrossbow c shoot erweapon projectile c shoot erweapon c actor c "
    "primalitem weapon crossbow c primalitemweapongenericgun c";
static const char* TekRifle = "weaptekrifle c shoot erweapon projectile c primalitem weapon tekrifle c";
static const char* TekGrenadeLauncher =
    "weaptekgrenadelauncher c shoot erweapon projectile c shoot erweapon c actor c "
    "primalitem weapon tekgrenadelauncher c";
static const char* RocketLauncher = "weaprocketlauncher c shoot erweapon projectile c";
static const char* WoodBow = "primalweaponbow c shoot erweapon projectile c primalitem weapon bow c";
static const char* CompoundBow = "weapcompoundbow c primalweaponbow c shoot erweapon projectile c";
static const char* TekBow = "weaptekbow c primalweaponbow c shoot erweapon projectile c";
static const char* GrenadeArrowLauncher =
    "weapgrenadearrowlauncher c grenadelauncher c primalweaponbow c shoot erweapon projectile c";
static const char* TekBowExplosiveArrow =
    "weaptekbow c primalweaponbow c primalitemammo explosiv earrow c";

int main() {
    // 秒换弹武器族分类：必须覆盖用户点名的四类。
    assert(ClassifyReloadWeapon(PumpShotgun) == WeaponFamily::PumpShotgun);
    assert(ClassifyReloadWeapon(AssaultRifle) == WeaponFamily::Rifle);
    assert(ClassifyReloadWeapon(Crossbow) == WeaponFamily::Crossbow);
    assert(ClassifyReloadWeapon(TekGrenadeLauncher) == WeaponFamily::Launcher);
    // 泰克步枪不得被 "rifle" 抢先归类成普通步枪之外的类型；归入步枪族但可用筛选区分。
    assert(ClassifyReloadWeapon(TekRifle) == WeaponFamily::Rifle);
    assert(ClassifyReloadWeapon("WeapTekSniper_C") == WeaponFamily::OtherGun);
    assert(ClassifyReloadWeapon("WeapSniper_C") == WeaponFamily::OtherGun);
    // "launcher" 关键字不得把榴弹发射器判成「其他枪械」。
    assert(ClassifyReloadWeapon(RocketLauncher) == WeaponFamily::OtherGun);
    assert(ClassifyReloadWeapon("") == WeaponFamily::None);
    assert(ClassifyReloadWeapon("primalitem consumable rawmeat c") == WeaponFamily::None);

    // 弓族分类：木弓／复合弓／泰克复合弓三分。
    assert(ClassifyBow(WoodBow) == BowFamily::Wood);
    assert(ClassifyBow(CompoundBow) == BowFamily::Compound);
    assert(ClassifyBow(TekBow) == BowFamily::Tek);
    assert(ClassifyBow(PumpShotgun) == BowFamily::None);
    // 命中 "bow" 的模组弓按木弓开关处理。
    assert(ClassifyBow(GrenadeArrowLauncher) == BowFamily::Wood);
    // 泰克弓的爆炸箭弹药类名同样只影响弓种分类，不会误判成榴弹发射器。
    assert(ClassifyBow(TekBowExplosiveArrow) == BowFamily::Tek);
    assert(ClassifyReloadWeapon(TekBowExplosiveArrow) == WeaponFamily::Bow);

    // 武器族 → 开关映射；未枚举的族必须回落到关闭，避免误伤。
    g_Config::bInstantReloadPumpShotgun = true;
    g_Config::bInstantReloadLauncher = true;
    g_Config::bInstantReloadRifle = false;
    g_Config::bInstantReloadCrossbow = true;
    g_Config::bInstantReloadOther = true;
    assert(ReloadFamilyEnabled(WeaponFamily::PumpShotgun));
    assert(ReloadFamilyEnabled(WeaponFamily::Launcher));
    assert(!ReloadFamilyEnabled(WeaponFamily::Rifle));
    assert(ReloadFamilyEnabled(WeaponFamily::Crossbow));
    assert(ReloadFamilyEnabled(WeaponFamily::OtherGun));
    assert(!ReloadFamilyEnabled(WeaponFamily::None));
    assert(!ReloadFamilyEnabled(WeaponFamily::Bow));

    g_Config::bFastBowWood = true;
    g_Config::bFastBowCompound = false;
    g_Config::bFastBowTek = true;
    assert(BowFamilyEnabled(BowFamily::Wood));
    assert(!BowFamilyEnabled(BowFamily::Compound));
    assert(BowFamilyEnabled(BowFamily::Tek));
    assert(!BowFamilyEnabled(BowFamily::None));

    // 换弹字段计划：逐发装填与整体时长都必须归零；弹药保护项只在本地主机追加。
    // 远程换弹重试间隔必须足够小以掩盖本地退弹动画，同时不至于每帧发包。
    assert(!CanPatchReload(false, WeaponFamily::PumpShotgun));
    assert(!CanPatchReload(true, WeaponFamily::Launcher));
    assert(CanPatchReload(true, WeaponFamily::PumpShotgun));
    assert(!CanPatchReload(true, WeaponFamily::Bow));

    // 弹夹容量清洗：非法值回落到当前值，再回落到 1。
    assert(SanitizeClipCapacity(6, 1) == 6);
    assert(SanitizeClipCapacity(512, 1) == 512);
    assert(SanitizeClipCapacity(0, 5) == 5);
    assert(SanitizeClipCapacity(-3, 5) == 5);
    assert(SanitizeClipCapacity(100000, 7) == 7);
    assert(SanitizeClipCapacity(0, 0) == 1);
    assert(SanitizeClipCapacity(0, -9) == 1);

    // 配置持久化：新开关必须存得进、读得回，缺键时保留默认值。
    const auto Directory = fs::absolute("release/tests/instant-reload-config");
    auto& Manager = ConfigManager::Get();
    Manager.Initialize(Directory.string());
    g_Config::bInstantReload = true;
    g_Config::bInstantReloadPumpShotgun = false;
    g_Config::bInstantReloadLauncher = true;
    g_Config::bInstantReloadRifle = false;
    g_Config::bInstantReloadCrossbow = true;
    g_Config::bInstantReloadOther = true;
    g_Config::bInstantReloadProtectAmmo = false;
    g_Config::bFastBow = true;
    g_Config::bFastBowWood = false;
    g_Config::bFastBowCompound = true;
    g_Config::bFastBowTek = false;
    g_Config::bFastBowForceShot = true;
    g_Config::bInstantThrowLauncher = true;
    assert(Manager.SaveConfig("instant-reload.ini"));
    g_Config::bInstantReload = false;
    g_Config::bInstantReloadPumpShotgun = true;
    g_Config::bInstantReloadLauncher = false;
    g_Config::bInstantReloadRifle = true;
    g_Config::bInstantReloadCrossbow = false;
    g_Config::bInstantReloadOther = false;
    g_Config::bInstantReloadProtectAmmo = true;
    g_Config::bFastBow = false;
    g_Config::bFastBowWood = true;
    g_Config::bFastBowCompound = false;
    g_Config::bFastBowTek = true;
    g_Config::bFastBowForceShot = false;
    g_Config::bInstantThrowLauncher = false;
    assert(Manager.LoadConfig("instant-reload.ini"));
    assert(g_Config::bInstantReload);
    assert(!g_Config::bInstantReloadPumpShotgun && g_Config::bInstantReloadLauncher);
    assert(!g_Config::bInstantReloadRifle && g_Config::bInstantReloadCrossbow);
    assert(g_Config::bInstantReloadOther && !g_Config::bInstantReloadProtectAmmo);
    assert(g_Config::bFastBow && !g_Config::bFastBowWood);
    assert(g_Config::bFastBowCompound && !g_Config::bFastBowTek);
    assert(g_Config::bFastBowForceShot && g_Config::bInstantThrowLauncher);

    // ── NestedFieldPatch：换弹时长/免耗弹/拉弓时间共用的补丁器 ────────────────
    // 复刻 FWeaponData 的关键布局：AmmoPerClip(int) / bDontTakeAmmoOnReload(bool 位段) /
    // NoAnimReloadDuration(float) / ReloadDurationPerAmmoCount(float)。
    // 使用真实 CppSDK 类型（SDK::FProperty/FBoolProperty/FStructProperty/UClass），
    // 只自建描述符与宿主对象，与 tests/feature_expansion_patch.cpp 同思路。
    struct FakeWeaponData {
        int32_t AmmoPerClip = 6;
        bool bDontTakeAmmoOnReload = false;
        uint8_t Pad = 0;
        float NoAnimReloadDuration = 2.5f;
        float ReloadDurationPerAmmoCount = 0.5f;
    };
    struct FakeWeapon : SDK::UObject {
        FakeWeaponData WeaponConfig{};
        float PullingTimeForMaximumSpeed = 1.2f;
    };

    // FName 需要游戏名称表才能解析；离线回归里把 ASCII 字段名编码进
    // FName.ComparisonIndex/Number，并用可覆盖的 MatchesFieldName 比对
    // （生产代码用默认的 FName::ToString()）。
    auto PackName = [](const char* Text) {
        SDK::FName Name;
        SDK::uint32 Packed = 0, Checksum = 0;
        for (int i = 0; Text[i] && i < 4; ++i) Packed |= SDK::uint32(uint8_t(Text[i])) << (8 * i);
        for (int i = 0; Text[i]; ++i) Checksum += uint8_t(Text[i]) * 131u;
        Name.ComparisonIndex = SDK::int32(Packed);
        Name.Number = Checksum;
        return Name;
    };
    struct TestPatch : NestedFieldPatch {
        bool MatchesFieldName(const SDK::FName& Name, const char* Expected) const override {
            const SDK::FName Wanted = PackName(Expected);
            return Name.ComparisonIndex == Wanted.ComparisonIndex && Name.Number == Wanted.Number;
        }
        bool MatchesClassName(const SDK::FFieldClass* FieldClass, const char* Expected) const override {
            if (!FieldClass) return false;
            const SDK::FName Wanted = PackName(Expected);
            return FieldClass->Name.ComparisonIndex == Wanted.ComparisonIndex &&
                FieldClass->Name.Number == Wanted.Number;
        }
        static SDK::FName PackName(const char* Text) {
            SDK::FName Name;
            SDK::uint32 Packed = 0, Checksum = 0;
            for (int i = 0; Text[i] && i < 4; ++i) Packed |= SDK::uint32(uint8_t(Text[i])) << (8 * i);
            for (int i = 0; Text[i]; ++i) Checksum += uint8_t(Text[i]) * 131u;
            Name.ComparisonIndex = SDK::int32(Packed);
            Name.Number = Checksum;
            return Name;
        }
    };

    SDK::FFieldClass FloatClass{PackName("FloatProperty")}, IntClass{PackName("IntProperty")},
        BoolClass{PackName("BoolProperty")}, StructClass{PackName("StructProperty")},
        WrongClass{PackName("StrProperty")};
    FakeWeapon WeaponAStorage{};
    FakeWeapon* WeaponA = &WeaponAStorage;
    FakeWeapon WeaponBStorage{};
    FakeWeapon* WeaponB = &WeaponBStorage;
    SDK::UClass WeaponDataStruct{}, WeaponClass{};
    SDK::FProperty AmmoPerClip{}, NoAnim{}, PerAmmo{}, TopLevelPull{};
    SDK::FBoolProperty TakeAmmo{};
    SDK::FStructProperty WeaponConfigProperty{};
    SDK::FField* Chain = nullptr;
    auto Link = [&Chain](SDK::FField* Field) { Field->Next = Chain; Chain = Field; };

    // SDK 属性类型没有可依赖的默认成员初值，ArrayDim/ElementSize/Offset 必须显式设置。
    WeaponDataStruct.Size = sizeof(FakeWeaponData);
    AmmoPerClip.Name = TestPatch::PackName("AmmoPerClip"); AmmoPerClip.ClassPrivate = &IntClass;
    AmmoPerClip.ArrayDim = 1; AmmoPerClip.ElementSize = sizeof(int32_t);
    AmmoPerClip.Offset = int(offsetof(FakeWeaponData, AmmoPerClip));
    TakeAmmo.Name = TestPatch::PackName("bDontTakeAmmoOnReload"); TakeAmmo.ClassPrivate = &BoolClass;
    TakeAmmo.ArrayDim = 1; TakeAmmo.ElementSize = 1;
    TakeAmmo.Offset = int(offsetof(FakeWeaponData, bDontTakeAmmoOnReload));
    TakeAmmo.ByteOffset = 0; TakeAmmo.FieldMask = TakeAmmo.ByteMask = 1;
    NoAnim.Name = TestPatch::PackName("NoAnimReloadDuration"); NoAnim.ClassPrivate = &FloatClass;
    NoAnim.ArrayDim = 1; NoAnim.ElementSize = sizeof(float);
    NoAnim.Offset = int(offsetof(FakeWeaponData, NoAnimReloadDuration));
    PerAmmo.Name = TestPatch::PackName("ReloadDurationPerAmmoCount"); PerAmmo.ClassPrivate = &FloatClass;
    PerAmmo.ArrayDim = 1; PerAmmo.ElementSize = sizeof(float);
    PerAmmo.Offset = int(offsetof(FakeWeaponData, ReloadDurationPerAmmoCount));
    Link(&AmmoPerClip); Link(&TakeAmmo); Link(&NoAnim); Link(&PerAmmo);
    WeaponDataStruct.ChildProperties = Chain;

    WeaponClass.Size = sizeof(FakeWeapon);
    WeaponConfigProperty.Name = TestPatch::PackName("WeaponConfig"); WeaponConfigProperty.ClassPrivate = &StructClass;
    WeaponConfigProperty.ArrayDim = 1; WeaponConfigProperty.ElementSize = sizeof(FakeWeaponData);
    WeaponConfigProperty.Offset = int(offsetof(FakeWeapon, WeaponConfig));
    WeaponConfigProperty.Struct = &WeaponDataStruct;
    TopLevelPull.Name = TestPatch::PackName("PullingTimeForMaximumSpeed"); TopLevelPull.ClassPrivate = &FloatClass;
    TopLevelPull.ArrayDim = 1; TopLevelPull.ElementSize = sizeof(float);
    TopLevelPull.Offset = int(offsetof(FakeWeapon, PullingTimeForMaximumSpeed));
    Chain = nullptr; Link(&WeaponConfigProperty); Link(&TopLevelPull);
    WeaponClass.ChildProperties = Chain;
    WeaponA->Class = WeaponB->Class = &WeaponClass;
    WeaponA->Index = 11; WeaponB->Index = 12;

    // 顶层浮点：应用 → 外部改写成为新基线 → 恢复。
    TestPatch Pull;
    Pull.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.001f);
    assert(Pull.Active && WeaponA->PullingTimeForMaximumSpeed == 0.001f);
    Pull.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.001f);
    assert(WeaponA->PullingTimeForMaximumSpeed == 0.001f); // 不累积
    Pull.Restore();
    assert(WeaponA->PullingTimeForMaximumSpeed == 1.2f);
    Pull.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.001f);
    WeaponA->PullingTimeForMaximumSpeed = 0.9f; // 游戏本体改写
    Pull.Restore();
    assert(WeaponA->PullingTimeForMaximumSpeed == 0.9f); // 以外部值为基线，不覆盖
    // 顶层浮点：名字不存在 / 类型不符都不得写入。
    Pull.ApplyNumber(WeaponA, nullptr, "NoSuchField", 0.0f);
    assert(!Pull.Active && WeaponA->PullingTimeForMaximumSpeed == 0.9f);
    SDK::FFieldClass* SavedPullClass = TopLevelPull.ClassPrivate;
    TopLevelPull.ClassPrivate = &WrongClass;
    Pull.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.0f);
    assert(!Pull.Active && WeaponA->PullingTimeForMaximumSpeed == 0.9f);
    TopLevelPull.ClassPrivate = SavedPullClass;

    // 结构体内浮点（换弹时长）：应用与恢复。
    TestPatch Duration;
    Duration.ApplyNumber(WeaponA, "WeaponConfig", "NoAnimReloadDuration", 0.0f);
    assert(Duration.Active && WeaponA->WeaponConfig.NoAnimReloadDuration == 0.0f);
    Duration.ApplyNumber(WeaponA, "WeaponConfig", "ReloadDurationPerAmmoCount", 0.0f);
    assert(WeaponA->WeaponConfig.ReloadDurationPerAmmoCount == 0.0f);
    Duration.Restore();
    assert(WeaponA->WeaponConfig.NoAnimReloadDuration == 2.5f);
    assert(WeaponA->WeaponConfig.ReloadDurationPerAmmoCount == 0.5f);

    // 结构体内 bool 位段（换弹不消耗弹药）——上一轮 review 指出的静默失效点。
    TestPatch TakeAmmoPatch;
    TakeAmmoPatch.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    assert(TakeAmmoPatch.Active);
    assert(WeaponA->WeaponConfig.bDontTakeAmmoOnReload);
    assert(WeaponA->WeaponConfig.Pad == 0); // 只改目标位段，不碰相邻字节
    TakeAmmoPatch.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    assert(WeaponA->WeaponConfig.bDontTakeAmmoOnReload);
    TakeAmmoPatch.Restore();
    assert(!WeaponA->WeaponConfig.bDontTakeAmmoOnReload);
    // 外部已改写为 false 时不得强行写回。
    TakeAmmoPatch.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    WeaponA->WeaponConfig.bDontTakeAmmoOnReload = false;
    TakeAmmoPatch.Restore();
    assert(!WeaponA->WeaponConfig.bDontTakeAmmoOnReload);
    // 换到另一把武器：旧对象恢复、新对象接手。
    TakeAmmoPatch.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    TakeAmmoPatch.ApplyBool(WeaponB, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    assert(!WeaponA->WeaponConfig.bDontTakeAmmoOnReload && WeaponB->WeaponConfig.bDontTakeAmmoOnReload);
    TakeAmmoPatch.Restore();
    assert(!WeaponB->WeaponConfig.bDontTakeAmmoOnReload);

    // 边界与身份校验：内层越界 / 掩码为 0 / 索引变化（槽位复用）都不得写入。
    TestPatch Guard;
    const int SavedTakeOffset = TakeAmmo.Offset;
    const uint8_t SavedMask = TakeAmmo.FieldMask;
    const int SavedStructSize = WeaponDataStruct.Size;
    TakeAmmo.Offset = SavedStructSize; // 内层落在结构体之外
    Guard.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    assert(!Guard.Active && !WeaponA->WeaponConfig.bDontTakeAmmoOnReload);
    TakeAmmo.Offset = SavedTakeOffset;
    TakeAmmo.FieldMask = TakeAmmo.ByteMask = 0;
    Guard.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    assert(!Guard.Active);
    TakeAmmo.FieldMask = TakeAmmo.ByteMask = SavedMask;
    Guard.ApplyBool(WeaponA, "WeaponConfig", "bDontTakeAmmoOnReload", true);
    assert(Guard.Active && Guard.SameOwner(WeaponA));
    WeaponA->Index = 99; // 槽位复用：索引变了就不再算同一对象
    assert(!Guard.SameOwner(WeaponA));
    Guard.Restore();
    assert(WeaponA->WeaponConfig.bDontTakeAmmoOnReload); // stale identity must not be written
    WeaponA->WeaponConfig.bDontTakeAmmoOnReload = false;
    WeaponA->Index = 11;
    // 实例内偏移越界（类尺寸变小）不得写入。
    const int SavedClassSize = WeaponClass.Size;
    WeaponClass.Size = WeaponConfigProperty.Offset + 1;
    Guard.ApplyNumber(WeaponA, "WeaponConfig", "NoAnimReloadDuration", 0.0f);
    assert(!Guard.Active);
    WeaponClass.Size = SavedClassSize;

    // Same address/index with a new serial is a different object; never restore into it.
    static int LiveSerial = 42;
    static bool Registered = true;
    Guard.Alive = [](const SDK::UObject*, int, int* Serial) {
        if (!Registered) return false;
        *Serial = LiveSerial;
        return true;
    };
    Guard.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.001f);
    assert(Guard.Active && Guard.OwnerSerial == 42);
    ++LiveSerial;
    Guard.Restore();
    assert(WeaponA->PullingTimeForMaximumSpeed == 0.001f);
    WeaponA->PullingTimeForMaximumSpeed = 1.2f;
    Guard.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.001f);
    Guard.Restore();
    assert(WeaponA->PullingTimeForMaximumSpeed == 1.2f);
    Registered = false;
    Guard.ApplyNumber(WeaponA, nullptr, "PullingTimeForMaximumSpeed", 0.001f);
    assert(!Guard.Active && WeaponA->PullingTimeForMaximumSpeed == 1.2f);

    std::cout << "Instant reload / bow charge rules: weapon families, per-family gating, "
                 "clip sanitising, configuration "
                 "persistence and nested-field patch (bool bit-field / float / top-level, "
                 "bounds, external-rewrite baselines, weapon switch) passed.\n";
}
