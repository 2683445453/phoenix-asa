// Execute the production resolver against real SDK layouts. The registry,
// reflected names and native class lookup are fixtures; there is no injection.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../src/external/CppSDK/SDK.hpp"
#include <cassert>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace g_Hack {
#include "../src/internal/Hack/WeaponWaterAccess.inl"
}

namespace {
    SDK::UClass Meta{}, ObjectClass{}, ItemClass{}, WeaponClass{}, InstantClass{}, RifleClass{}, TekClass{}, MeleeClass{}, WorldClass{}, LargeOtherClass{};
    SDK::TUObjectArray Objects{};
    SDK::FUObjectItem Entries[64]{};
    SDK::FUObjectItem* Chunks[]{Entries};
    std::vector<std::wstring> Names{L"None"};
    SDK::FName Name(const wchar_t* Text) {
        for (size_t I = 0; I < Names.size(); ++I) if (Names[I] == Text) return SDK::FName(int32_t(I));
        Names.emplace_back(Text); return SDK::FName(int32_t(Names.size() - 1));
    }
    const void* NameEntry(uint32_t Index) {
        assert(Index < Names.size()); return Names[Index].c_str();
    }
    void AppendName(const void* Entry, SDK::FString& Output) {
        Output = SDK::FString(static_cast<const wchar_t*>(Entry));
    }
    void Serial(int32_t Index, int32_t Value) {
        std::memcpy(reinterpret_cast<uint8_t*>(&Entries[Index]) + 0x10, &Value, sizeof(Value));
    }
    void Register(SDK::UObject& Object, SDK::UClass* Class) {
        Object.Index = Objects.NumElements++; Object.Class = Class;
        Entries[Object.Index].Object = &Object; Serial(Object.Index, 100 + Object.Index);
    }
    void Class(SDK::UClass& Type, const wchar_t* Text, SDK::UClass* Base, int32_t Size) {
        Register(Type, &Meta); Type.Name = Name(Text); Type.SuperStruct = Base; Type.Size = Size;
    }
    void Soft(SDK::UPrimalItem& Item, SDK::UObject* Target) {
        Item.WeaponTemplate.WeakPtr.ObjectIndex = Target ? Target->Index : -1;
        Item.WeaponTemplate.WeakPtr.ObjectSerialNumber = Target ? 100 + Target->Index : 0;
    }
    void Bool(SDK::FBoolProperty& Field, SDK::FFieldClass* Type, const wchar_t* Text, int32_t Offset, uint8_t Mask) {
        Field.Name = Name(Text); Field.ClassPrivate = Type; Field.ArrayDim = 1; Field.ElementSize = 1;
        Field.Offset = Offset; Field.FieldSize = 1; Field.ByteOffset = 0; Field.ByteMask = Field.FieldMask = Mask;
    }
}
namespace SDK {
    uintptr_t InSDKUtils::GetImageBase() { assert(false); return 0; }
    UClass* BasicFilesImplUtils::FindClassByName(const std::string& Name, bool) {
        if (Name == "Class") return &Meta;
        if (Name == "PrimalItem") return &ItemClass;
        if (Name == "ShooterWeapon") return &WeaponClass;
        if (Name == "ShooterWeapon_Instant") return &InstantClass;
        if (Name == "World") return &WorldClass;
        assert(false); return nullptr;
    }
}

int main(int Argc, char** Argv) {
    using namespace g_Hack::WaterAccess;
    Objects.Objects = Chunks; Objects.MaxElements = 64; Objects.NumElements = 1;
    Objects.MaxChunks = Objects.NumChunks = 1;
    SDK::UObject::GObjects.InitManually(&Objects);
    SDK::FName::GetNameEntryFromName = reinterpret_cast<void*>(&NameEntry);
    SDK::FName::AppendString = reinterpret_cast<void*>(&AppendName);
    Class(Meta, L"Class", nullptr, sizeof(SDK::UClass));
    Class(ObjectClass, L"Object", nullptr, sizeof(SDK::UObject));
    Class(ItemClass, L"PrimalItem", &ObjectClass, sizeof(SDK::UPrimalItem));
    Class(WeaponClass, L"ShooterWeapon", &ObjectClass, sizeof(SDK::AShooterWeapon));
    Class(InstantClass, L"ShooterWeapon_Instant", &WeaponClass, sizeof(SDK::AShooterWeapon_Instant));
    Class(RifleClass, L"WeapRifle_C", &InstantClass, sizeof(SDK::AShooterWeapon_Instant));
    Class(TekClass, L"WeapTekPistol_C", &WeaponClass, sizeof(SDK::AShooterWeapon));
    Class(MeleeClass, L"WeapSword_C", &WeaponClass, sizeof(SDK::AShooterWeapon));
    Class(WorldClass, L"World", &ObjectClass, sizeof(SDK::UWorld));
    Class(LargeOtherClass, L"LargeNonItem", &ObjectClass, sizeof(SDK::UPrimalItem));
    static SDK::UPrimalItem Item{};
    static SDK::AShooterWeapon_Instant Rifle{}, Default{}, Another{};
    static SDK::AShooterWeapon Tek{}, Melee{};
    static SDK::UObject Other{};
    static SDK::UPrimalItem LargeOther{};
    static SDK::UWorld World{};
    Register(Item, &ItemClass); Register(Rifle, &RifleClass); Register(Default, &RifleClass);
    Register(Another, &RifleClass); Register(Tek, &TekClass); Register(Melee, &MeleeClass); Register(Other, &ObjectClass);
    Register(LargeOther, &LargeOtherClass); Register(World, &WorldClass);
    Item.MyItemType = SDK::EPrimalItemType::Weapon;
    Default.Flags = SDK::EObjectFlags::ClassDefaultObject; RifleClass.ClassDefaultObject = &Default;
    SDK::FFieldClass SoftType{}, WrongType{}, BoolType{};
    SoftType.Name = Name(L"SoftClassProperty"); WrongType.Name = Name(L"SoftObjectProperty"); BoolType.Name = Name(L"BoolProperty");
    SDK::FProperty Template{};
    Template.Name = Name(L"WeaponTemplate"); Template.ClassPrivate = &SoftType;
    Template.ArrayDim = 1; Template.ElementSize = sizeof(Item.WeaponTemplate);
    Template.Offset = int32_t(reinterpret_cast<uint8_t*>(&Item.WeaponTemplate) - reinterpret_cast<uint8_t*>(&Item));
    ItemClass.ChildProperties = &Template;
    SDK::FBoolProperty Firing{}, Equipping{};
    Bool(Firing, &BoolType, L"bAllowSubmergedFiring", 0xCC8, 1);
    Bool(Equipping, &BoolType, L"bPreventEquippingUnderwater", 0xDD5, 2);
    WeaponClass.ChildProperties = &Firing; Firing.Next = &Equipping;
    SDK::FFieldClass DoubleType{};
    DoubleType.Name = Name(L"DoubleProperty");
    SDK::FProperty TimeField{}, CreationField{};
    TimeField.Name = Name(L"TimeSeconds"); TimeField.ClassPrivate = &DoubleType;
    TimeField.ArrayDim = 1; TimeField.ElementSize = sizeof(double);
    TimeField.Offset = int32_t(reinterpret_cast<uint8_t*>(&World.TimeSeconds) - reinterpret_cast<uint8_t*>(&World));
    CreationField.Name = Name(L"CreationTime"); CreationField.ClassPrivate = &DoubleType;
    CreationField.ArrayDim = 1; CreationField.ElementSize = sizeof(double);
    CreationField.Offset = int32_t(reinterpret_cast<uint8_t*>(&Rifle.CreationTime) - reinterpret_cast<uint8_t*>(&Rifle));
    WorldClass.ChildProperties = &TimeField; Equipping.Next = &CreationField;

    if (Argc > 1 && std::strcmp(Argv[1], "--benchmark") == 0) {
        SDK::UClass ItemDerived[4]{};
        SDK::UClass* Parent = &ItemClass;
        for (auto& Type : ItemDerived) { Class(Type, L"ResourceSubclass", Parent, sizeof(SDK::UPrimalItem)); Parent = &Type; }
        Item.Class = Parent; Soft(Item, &RifleClass);
        const auto Measure = [&](int Count, SDK::EPrimalItemType Type, bool Cached) {
            Item.MyItemType = Type;
            const auto Start = std::chrono::steady_clock::now();
            const auto Check = [&] { for (int I = 0; I < Count; ++I) {
                    auto* Result = ItemWeaponClass(&Item);
                    assert(Result == (Type == SDK::EPrimalItemType::Weapon ? &RifleClass : nullptr));
                } };
            if (Cached) { ValidationPass Pass; Check(); } else Check();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        };
        std::cout << "Native WaterAccess benchmark (four derived item classes): uncached 500 resources=" << Measure(500, SDK::EPrimalItemType::Resource, false)
            << " ms; 50 weapons=" << Measure(50, SDK::EPrimalItemType::Weapon, false) << " ms.\n";
        std::cout << "Scoped metadata cache: 500 resources=" << Measure(500, SDK::EPrimalItemType::Resource, true)
            << " ms; 50 weapons=" << Measure(50, SDK::EPrimalItemType::Weapon, true) << " ms.\n";
        Flag Fire{}, Equip{}; assert(GetFlags(&Rifle, Fire, Equip));
        const auto Start = std::chrono::steady_clock::now();
        for (int I = 0; I < 100; ++I) {
            ValidationPass Pass;
            assert(CapturedWeapon(&Rifle, &RifleClass, Rifle.Index, 100 + Rifle.Index, RifleClass.Index, 100 + RifleClass.Index, false));
            assert(Fire.AccessValidated(&Rifle, true) && Equip.AccessValidated(&Rifle, true));
            const auto FireValue = Fire.ReadValidated(&Rifle), EquipValue = Equip.ReadValidated(&Rifle);
            Fire.WriteValidated(&Rifle, FireValue); Equip.WriteValidated(&Rifle, EquipValue);
        }
        std::cout << "100 independent callbacks captured weapon + both flags="
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count() << " ms.\n";
        return 0;
    }

    auto* Poison = reinterpret_cast<SDK::UObject*>(uintptr_t(0x1000000000));
    assert(!Memory(Poison, sizeof(SDK::UObject)));
    assert(!IsLive(nullptr) && !IsLive(Poison));
    assert(IsLive(&Item) && IsLive(&Rifle) && !IsLive(&Default) && IsLive(&Default, true));
    Soft(Item, &RifleClass);
    assert(ItemWeaponClass(&Item) == &RifleClass && DefaultWeapon(&RifleClass) == &Default);
    assert(IsSupported(&Rifle) && IsSupported(&Default) && IsSupported(&Tek) && !IsSupported(&Melee));
    // Native reflection metadata often has no weak serial yet. It must remain
    // usable for an immediate type check without weakening soft-reference rules.
    for (auto* Type : {&Meta, &ObjectClass, &ItemClass, &WeaponClass, &InstantClass}) Serial(Type->Index, 0);
    assert(ItemWeaponClass(&Item) == &RifleClass && IsSupported(&Rifle));
    Flag ZeroMetadataFire{}, ZeroMetadataEquip{};
    assert(GetFlags(&Rifle, ZeroMetadataFire, ZeroMetadataEquip));
    Serial(Default.Index, 0);
    assert(IsLive(&Default, true) && DefaultWeapon(&RifleClass) == &Default);
    Serial(Default.Index, 100 + Default.Index);
    for (auto* Type : {&Meta, &ObjectClass, &ItemClass, &WeaponClass, &InstantClass}) Serial(Type->Index, 100 + Type->Index);
    assert(!ItemWeaponClass(reinterpret_cast<SDK::UPrimalItem*>(&Other)));
    LargeOther.MyItemType = SDK::EPrimalItemType::Weapon; Soft(LargeOther, &RifleClass);
    assert(!ItemWeaponClass(&LargeOther)); // plausible prefilter bytes still cannot bypass ancestry
    assert(!DefaultWeapon(reinterpret_cast<SDK::UClass*>(&Other)));

    for (auto Type : {SDK::EPrimalItemType::Resource, SDK::EPrimalItemType::Skin, SDK::EPrimalItemType::Equipment}) {
        Item.MyItemType = Type; assert(!ItemWeaponClass(&Item));
    }
    Item.MyItemType = SDK::EPrimalItemType::Weapon;
    Item.bIsBlueprint = true; assert(!ItemWeaponClass(&Item)); Item.bIsBlueprint = false;
    Item.bIsEngram = true; assert(!ItemWeaponClass(&Item)); Item.bIsEngram = false;
    Soft(Item, nullptr); assert(!ItemWeaponClass(&Item));
    Item.WeaponTemplate.WeakPtr.ObjectIndex = RifleClass.Index;
    assert(!ItemWeaponClass(&Item)); // zero serial never resolves an occupied slot
    Soft(Item, &RifleClass); ++Item.WeaponTemplate.WeakPtr.ObjectSerialNumber; assert(!ItemWeaponClass(&Item));
    Soft(Item, &Other); assert(!ItemWeaponClass(&Item)); // live non-UClass must not expose a fake CDO
    Soft(Item, &ItemClass); assert(!ItemWeaponClass(&Item)); // live class, wrong ancestry
    Soft(Item, &RifleClass);
    Entries[RifleClass.Index].Object = Poison; assert(!ItemWeaponClass(&Item)); Entries[RifleClass.Index].Object = &RifleClass;
    Serial(RifleClass.Index, 0); assert(!ItemWeaponClass(&Item)); Serial(RifleClass.Index, 100 + RifleClass.Index);
    auto* SavedChunk = Chunks[0]; Chunks[0] = reinterpret_cast<SDK::FUObjectItem*>(Poison);
    assert(!ItemWeaponClass(&Item)); Chunks[0] = SavedChunk;
    Template.ClassPrivate = &WrongType; assert(!ItemWeaponClass(&Item)); Template.ClassPrivate = &SoftType;
    Template.ElementSize += 8; assert(!ItemWeaponClass(&Item)); Template.ElementSize -= 8;
    Template.ArrayDim = 2; assert(!ItemWeaponClass(&Item)); Template.ArrayDim = 1;
    const auto OriginalOffset = Template.Offset;
    for (int32_t Offset : {-1, 0, int32_t(sizeof(Item) - 1), INT32_MAX}) {
        Template.Offset = Offset; assert(!ItemWeaponClass(&Item));
    }
    Template.Offset = OriginalOffset;
    ItemClass.ChildProperties = reinterpret_cast<SDK::FField*>(Poison); assert(!ItemWeaponClass(&Item)); ItemClass.ChildProperties = &Template;
    auto* SavedNameClass = Rifle.Class; Rifle.Class = reinterpret_cast<SDK::UClass*>(Poison);
    assert(!IsSupported(&Rifle)); Rifle.Class = SavedNameClass;
    RifleClass.ClassDefaultObject = Poison; assert(!DefaultWeapon(&RifleClass));
    RifleClass.ClassDefaultObject = &Other; assert(!DefaultWeapon(&RifleClass));
    RifleClass.ClassDefaultObject = &Rifle; assert(!DefaultWeapon(&RifleClass));
    RifleClass.ClassDefaultObject = &Default;
    Default.Class = &InstantClass; assert(!DefaultWeapon(&RifleClass)); Default.Class = &RifleClass;
    Default.Flags |= SDK::EObjectFlags::BeginDestroyed; assert(!DefaultWeapon(&RifleClass)); Default.Flags = SDK::EObjectFlags::ClassDefaultObject;

    double Time = 0;
    World.TimeSeconds = 100000200.5; Rifle.CreationTime = 100000100.25;
    assert(WorldTime(&World, Time) && Time == World.TimeSeconds);
    assert(CreationTime(&Rifle, Time) && Time == Rifle.CreationTime);
    assert(!CreationTime(&Default, Time) && Time == -1);
    assert(!WorldTime(reinterpret_cast<SDK::UWorld*>(&Other), Time) && Time == -1);
    assert(!WorldTime(reinterpret_cast<SDK::UWorld*>(Poison), Time) && Time == -1);
    for (double Invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        World.TimeSeconds = Invalid; Rifle.CreationTime = Invalid;
        assert(!WorldTime(&World, Time) && Time == -1);
        assert(!CreationTime(&Rifle, Time) && Time == -1);
    }
    World.TimeSeconds = 100000200.5; Rifle.CreationTime = 100000100.25;
    CreationField.ClassPrivate = &WrongType; assert(!CreationTime(&Rifle, Time)); CreationField.ClassPrivate = &DoubleType;
    TimeField.Offset = sizeof(World); assert(!WorldTime(&World, Time));
    TimeField.Offset = int32_t(reinterpret_cast<uint8_t*>(&World.TimeSeconds) - reinterpret_cast<uint8_t*>(&World));

    Flag Fire{}, Equip{};
    assert(GetFlags(&Rifle, Fire, Equip));
    auto* Bytes = reinterpret_cast<uint8_t*>(&Rifle);
    Bytes[Fire.Offset] = 0xA4; Bytes[Equip.Offset] = 0xB6;
    assert(!Fire.Read(&Rifle) && Equip.Read(&Rifle));
    Fire.Write(&Rifle, true); Equip.Write(&Rifle, false);
    assert(Bytes[Fire.Offset] == 0xA5 && Bytes[Equip.Offset] == 0xB4);
    Fire.Write(&Rifle, false); Equip.Write(&Rifle, true);
    assert(Bytes[Fire.Offset] == 0xA4 && Bytes[Equip.Offset] == 0xB6); // adjacent flags preserved
    assert(GetFlags(&Default, Fire, Equip)); Fire.Write(&Default, true); assert(Fire.Read(&Default));
    assert(CapturedWeapon(&Rifle, &RifleClass, Rifle.Index, 100 + Rifle.Index, RifleClass.Index, 100 + RifleClass.Index, false));
    assert(Fire.AccessValidated(&Rifle, true));
    Fire.WriteValidated(&Rifle, true); assert(Fire.ReadValidated(&Rifle));
    Fire.WriteValidated(&Rifle, false);
    assert(!CapturedWeapon(&Rifle, &RifleClass, Rifle.Index, 0, RifleClass.Index, 100 + RifleClass.Index, false));
    assert(!CapturedWeapon(&Rifle, &RifleClass, Rifle.Index, 101 + Rifle.Index, RifleClass.Index, 100 + RifleClass.Index, false));
    assert(!CapturedWeapon(&Rifle, &RifleClass, Rifle.Index, 100 + Rifle.Index, RifleClass.Index, 101 + RifleClass.Index, false));
    Serial(Default.Index, 0);
    assert(CapturedWeapon(&Default, &RifleClass, Default.Index, 0, RifleClass.Index, 100 + RifleClass.Index, true));
    assert(!CapturedWeapon(&Default, &RifleClass, Default.Index, 0, RifleClass.Index, 0, true));
    Serial(Default.Index, 100 + Default.Index);
    Firing.Offset += 1; assert(!Fire.AccessValidated(&Rifle, true)); Firing.Offset -= 1;
    Firing.FieldMask = Firing.ByteMask = 2; assert(!Fire.AccessValidated(&Rifle, true)); Firing.FieldMask = Firing.ByteMask = 1;
    RifleClass.ClassDefaultObject = &Another;
    assert(!CapturedWeapon(&Default, &RifleClass, Default.Index, 100 + Default.Index, RifleClass.Index, 100 + RifleClass.Index, true));
    RifleClass.ClassDefaultObject = &Default;
    Fire.Write(Poison, true); assert(!Fire.Read(Poison));
    Firing.FieldMask = Firing.ByteMask = 3; assert(!GetFlags(&Rifle, Fire, Equip));
    Firing.FieldMask = Firing.ByteMask = 1;
    Firing.ByteOffset = 1; assert(!GetFlags(&Rifle, Fire, Equip)); Firing.ByteOffset = 0;
    Firing.Offset = sizeof(Rifle); assert(!GetFlags(&Rifle, Fire, Equip)); Firing.Offset = 0xCC8;
    Firing.ClassPrivate = &WrongType; assert(!GetFlags(&Rifle, Fire, Equip)); Firing.ClassPrivate = &BoolType;
    Firing.Next = reinterpret_cast<SDK::FField*>(Poison); assert(!GetFlags(&Rifle, Fire, Equip)); Firing.Next = &Equipping;

    {
        ValidationPass Pass;
        assert(ItemWeaponClass(&Item) == &RifleClass && ItemWeaponClass(&Item) == &RifleClass);
        assert(Cache.ClassCount && Cache.DerivesCount && Cache.PropertyCount);
        Serial(RifleClass.Index, 101 + RifleClass.Index);
        assert(!ItemWeaponClass(&Item)); // registry generations are never memoized
        Serial(RifleClass.Index, 100 + RifleClass.Index);
        ClearCaches(); // emulate a metadata reload inside a test callback
        Template.ElementSize += 8; assert(!ItemWeaponClass(&Item)); Template.ElementSize -= 8;
        ClearCaches(); assert(ItemWeaponClass(&Item) == &RifleClass);
        { ValidationPass Nested; assert(ItemWeaponClass(&Item) == &RifleClass); }
        assert(Cache.Depth == 1);
    }
    assert(Cache.Depth == 0 && Cache.ClassCount == 0 && Cache.DerivesCount == 0 && Cache.PropertyCount == 0);

    void* Protected = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(Protected);
    DWORD Previous = 0;
    assert(VirtualProtect(Protected, 4096, PAGE_NOACCESS, &Previous));
    assert(!Memory(Protected, 1) && !IsLive(static_cast<SDK::UObject*>(Protected)));
    assert(VirtualProtect(Protected, 4096, PAGE_READONLY, &Previous));
    assert(Memory(Protected, 4096) && !Memory(Protected, 1, true));
    assert(!Memory(reinterpret_cast<const void*>(UINTPTR_MAX - 1), 8));
    assert(VirtualFree(Protected, 0, MEM_RELEASE));

    // Reused slot invalidates the old pointer even when its memory remains readable.
    Entries[Rifle.Index].Object = &Another;
    assert(!IsLive(&Rifle) && !GetFlags(&Rifle, Fire, Equip));
    std::cout << "Water access: observed poisoned address, null/stale/nonclass soft references, zero-serial metadata, registry reuse, reflected bounds/types/timestamps, CDO identity, destroyed objects and owned flag bytes passed.\n";
}
