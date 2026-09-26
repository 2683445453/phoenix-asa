// Included inside g_Hack. Run only on the serialized game thread: VirtualQuery
// rejects invalid memory, while registry identity/serial checks reject stale UObjects.
// No generated weak/soft .Get() helper is used here (those ignore serial numbers).
namespace WaterAccess {
    struct ClassShape {
        SDK::UClass* Pointer = nullptr;
        int32_t Index = -1, Serial = -1, Size = 0;
        SDK::UClass* Meta = nullptr;
        SDK::UStruct* Super = nullptr;
        SDK::FField* Fields = nullptr;
        SDK::FName Name{};
        bool operator==(const ClassShape& Other) const {
            return Pointer == Other.Pointer && Index == Other.Index && Serial == Other.Serial &&
                Size == Other.Size && Meta == Other.Meta && Super == Other.Super && Fields == Other.Fields && Name == Other.Name;
        }
    };
    struct ClassMemo { ClassShape Shape; bool Result = false; };
    struct DerivesMemo { ClassShape Shape, Base; bool Result = false; };
    struct PropertyMemo {
        ClassShape Shape;
        std::string Name;
        SDK::FProperty* Property = nullptr;
    };
    struct ValidationCache {
        unsigned Depth = 0;
        size_t ClassCount = 0, DerivesCount = 0, PropertyCount = 0;
        std::array<ClassMemo, 128> Classes{};
        std::array<DerivesMemo, 128> Derivations{};
        std::array<PropertyMemo, 128> Properties{};
    };
    // The surrounding runtime already serializes all access on the game thread.
    // Avoid introducing dynamic TLS requirements into an injected DLL.
    inline ValidationCache Cache;
    inline void ClearCaches() { Cache.ClassCount = Cache.DerivesCount = Cache.PropertyCount = 0; }
    struct ValidationPass {
        ValidationPass() { if (Cache.Depth++ == 0) ClearCaches(); }
        ~ValidationPass() { if (--Cache.Depth == 0) ClearCaches(); }
        ValidationPass(const ValidationPass&) = delete;
        ValidationPass& operator=(const ValidationPass&) = delete;
    };
    // Only immutable reflection lookups are memoized during one serialized
    // callback. Registry identities and memory protections are checked afresh.
    // Never keep a pass alive across callbacks, engine dispatch or asset loads.
    inline bool Memory(const void* Pointer, size_t Length, bool Writable = false) {
        if (!Pointer || !Length) return false;
        auto Address = reinterpret_cast<uintptr_t>(Pointer);
        if (Address > UINTPTR_MAX - Length) return false;
        const auto End = Address + Length;
        while (Address < End) {
            MEMORY_BASIC_INFORMATION Region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(Address), &Region, sizeof(Region)) ||
                Region.State != MEM_COMMIT || (Region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const DWORD Protection = Region.Protect & 0xff;
            const bool CanWrite = Protection == PAGE_READWRITE || Protection == PAGE_WRITECOPY ||
                Protection == PAGE_EXECUTE_READWRITE || Protection == PAGE_EXECUTE_WRITECOPY;
            const bool CanRead = CanWrite || Protection == PAGE_READONLY || Protection == PAGE_EXECUTE_READ;
            if (!CanRead || (Writable && !CanWrite)) return false;
            const auto Start = reinterpret_cast<uintptr_t>(Region.BaseAddress);
            if (Start > UINTPTR_MAX - Region.RegionSize) return false;
            const auto RegionEnd = Start + Region.RegionSize;
            if (RegionEnd <= Address) return false;
            Address = RegionEnd < End ? RegionEnd : End;
        }
        return true;
    }

    inline bool Registry(int32_t Index, SDK::UObject*& Object, int32_t& Serial) {
        Object = nullptr; Serial = 0;
        const auto* Array = SDK::UObject::GObjects.GetTypedPtr();
        if (!Memory(Array, sizeof(*Array)) || Index < 0 || Index >= Array->NumElements ||
            Array->NumElements > Array->MaxElements || Array->NumChunks <= 0 ||
            Array->NumChunks > Array->MaxChunks) return false;
        const int32_t ChunkIndex = Index / SDK::TUObjectArray::ElementsPerChunk;
        const int32_t Within = Index % SDK::TUObjectArray::ElementsPerChunk;
        if (ChunkIndex >= Array->NumChunks) return false;
        auto** Chunks = Array->GetDecrytedObjPtr();
        const auto SlotAddress = reinterpret_cast<uintptr_t>(Chunks) + size_t(ChunkIndex) * sizeof(*Chunks);
        if (SlotAddress < reinterpret_cast<uintptr_t>(Chunks) ||
            !Memory(reinterpret_cast<const void*>(SlotAddress), sizeof(*Chunks))) return false;
        SDK::FUObjectItem* Chunk = nullptr;
        std::memcpy(&Chunk, reinterpret_cast<const void*>(SlotAddress), sizeof(Chunk));
        const auto EntryAddress = reinterpret_cast<uintptr_t>(Chunk) + size_t(Within) * sizeof(*Chunk);
        if (!Chunk || EntryAddress < reinterpret_cast<uintptr_t>(Chunk) ||
            !Memory(reinterpret_cast<const void*>(EntryAddress), sizeof(*Chunk))) return false;
        // ASA's verified 0x18-byte FUObjectItem stores its serial at 0x10.
        std::memcpy(&Object, reinterpret_cast<const void*>(EntryAddress), sizeof(Object));
        std::memcpy(&Serial, reinterpret_cast<const void*>(EntryAddress + 0x10), sizeof(Serial));
        // UE assigns weak serials lazily. A zero serial is valid for an
        // instantaneous registry/type check, especially for native metadata.
        // Weak reference resolution and retained mutation ownership must still
        // require their own positive, matching serial (or a live owning anchor).
        return Object && Serial >= 0;
    }

    inline bool LiveSnapshot(SDK::UObject* Object, bool AllowDefault, int32_t* OutSerial) {
        if (!Memory(Object, sizeof(SDK::UObject))) return false;
        SDK::UObject* Registered = nullptr;
        int32_t Serial = 0;
        if (!Registry(Object->Index, Registered, Serial) || Registered != Object || !Object->Class ||
            !Memory(Object->Class, sizeof(SDK::UObject))) return false;
        if (OutSerial) *OutSerial = Serial;
        const auto Flags = Object->Flags;
        return !(Flags & SDK::EObjectFlags::BeginDestroyed) &&
            !(Flags & SDK::EObjectFlags::FinishDestroyed) &&
            !(Flags & SDK::EObjectFlags::MirroredGarbage) &&
            (AllowDefault || !(Flags & SDK::EObjectFlags::ClassDefaultObject));
    }

    inline bool IsLive(SDK::UObject* Object, bool AllowDefault = false) {
        return LiveSnapshot(Object, AllowDefault, nullptr);
    }

    inline bool ReadClassShape(SDK::UClass* Class, ClassShape& Shape) {
        int32_t Serial = -1;
        if (!LiveSnapshot(Class, false, &Serial) || !Memory(Class, sizeof(SDK::UClass))) return false;
        Shape = {Class, Class->Index, Serial, Class->Size, Class->Class, Class->SuperStruct, Class->ChildProperties, Class->Name};
        return true;
    }

    inline bool IsClass(SDK::UClass* Class) {
        ClassShape Shape;
        if (!ReadClassShape(Class, Shape)) return false;
        if (Cache.Depth) for (size_t I = 0; I < Cache.ClassCount; ++I)
            if (Cache.Classes[I].Shape == Shape) return Cache.Classes[I].Result;
        const auto* MetaClass = SDK::UClass::StaticClass();
        if (!IsLive(const_cast<SDK::UClass*>(MetaClass)) || !Memory(MetaClass, sizeof(SDK::UClass))) return false;
        auto* Type = Class->Class;
        for (unsigned Depth = 0; Type && Depth < 128; ++Depth) {
            if (!IsLive(Type) || !Memory(Type, sizeof(SDK::UClass))) return false;
            if (Type == MetaClass) {
                if (Cache.Depth && Cache.ClassCount < Cache.Classes.size()) Cache.Classes[Cache.ClassCount++] = {Shape, true};
                return true;
            }
            Type = static_cast<SDK::UClass*>(Type->SuperStruct);
        }
        return false;
    }

    inline bool Derives(SDK::UClass* Class, SDK::UClass* Base) {
        ClassShape Shape, BaseShape;
        if (Cache.Depth) {
            if (!ReadClassShape(Class, Shape) || !ReadClassShape(Base, BaseShape)) return false;
            for (size_t I = 0; I < Cache.DerivesCount; ++I) {
                const auto& Entry = Cache.Derivations[I];
                if (Entry.Shape == Shape && Entry.Base == BaseShape) return Entry.Result;
            }
        }
        if (!IsClass(Class) || !IsClass(Base)) return false;
        for (unsigned Depth = 0; Class && Depth < 128; ++Depth) {
            if (!IsClass(Class)) return false;
            if (Class == Base) {
                if (Cache.Depth && Cache.DerivesCount < Cache.Derivations.size()) Cache.Derivations[Cache.DerivesCount++] = {Shape, BaseShape, true};
                return true;
            }
            Class = static_cast<SDK::UClass*>(Class->SuperStruct);
        }
        if (!Class && Cache.Depth && Cache.DerivesCount < Cache.Derivations.size())
            Cache.Derivations[Cache.DerivesCount++] = {Shape, BaseShape, false};
        return false;
    }

    inline SDK::FProperty* Property(SDK::UClass* Class, const char* Name) {
        ClassShape Shape;
        if (Cache.Depth) {
            if (!ReadClassShape(Class, Shape)) return nullptr;
            for (size_t I = 0; I < Cache.PropertyCount; ++I) {
                const auto& Entry = Cache.Properties[I];
                if (Entry.Shape == Shape && Entry.Name == Name && Memory(Entry.Property, sizeof(SDK::FProperty)) &&
                    Entry.Property->Name.ToString() == Name) return Entry.Property;
            }
        }
        for (unsigned Depth = 0; Class && Depth < 128; ++Depth) {
            if (!IsClass(Class)) return nullptr;
            auto* Field = Class->ChildProperties;
            for (unsigned Count = 0; Field && Count < 4096; ++Count) {
                if (!Memory(Field, sizeof(SDK::FProperty))) return nullptr;
                if (Field->Name.ToString() == Name) {
                    auto* Result = static_cast<SDK::FProperty*>(Field);
                    if (Cache.Depth && Cache.PropertyCount < Cache.Properties.size()) Cache.Properties[Cache.PropertyCount++] = {Shape, Name, Result};
                    return Result;
                }
                Field = Field->Next;
            }
            if (Field) return nullptr; // malformed/cyclic property list
            Class = static_cast<SDK::UClass*>(Class->SuperStruct);
        }
        return nullptr;
    }

    inline bool PropertyBounds(SDK::UObject* Object, SDK::FProperty* Field,
        const char* Type, size_t Size) {
        if (!IsLive(Object, true) || !IsClass(Object->Class) ||
            !Memory(Field, sizeof(SDK::FProperty)) || !Memory(Field->ClassPrivate, sizeof(SDK::FFieldClass)) ||
            Field->ClassPrivate->Name.ToString() != Type || Field->ArrayDim != 1 ||
            Field->ElementSize != Size || Field->Offset < int32_t(sizeof(SDK::UObject)) ||
            Object->Class->Size < Field->Offset || Size > size_t(Object->Class->Size - Field->Offset)) return false;
        return Memory(reinterpret_cast<const uint8_t*>(Object) + Field->Offset, Size);
    }

    inline SDK::UClass* ItemWeaponClass(SDK::UPrimalItem* Item) {
        // This is only a bounded, read-only rejection filter. Hundreds of
        // resource stacks should not repeat the full ancestor/reflection walk.
        // A matching byte never establishes item type: ancestry is still
        // mandatory before following any weapon template or mutating anything.
        if (!IsLive(Item) || !IsClass(Item->Class) ||
            Item->Class->Size < sizeof(SDK::UPrimalItem) || !Memory(Item, sizeof(SDK::UPrimalItem)) ||
            Item->MyItemType != SDK::EPrimalItemType::Weapon || Item->bIsBlueprint || Item->bIsEngram) return nullptr;
        if (!Derives(Item->Class, SDK::UPrimalItem::StaticClass())) return nullptr;
        auto* Field = Property(Item->Class, "WeaponTemplate");
        if (!PropertyBounds(Item, Field, "SoftClassProperty", sizeof(SDK::TSoftClassPtr<SDK::UClass>))) return nullptr;
        SDK::FWeakObjectPtr Weak{};
        std::memcpy(&Weak, reinterpret_cast<const uint8_t*>(Item) + Field->Offset, sizeof(Weak));
        if (Weak.ObjectIndex < 0 || Weak.ObjectSerialNumber <= 0) return nullptr;
        SDK::UObject* Object = nullptr;
        int32_t Serial = 0;
        if (!Registry(Weak.ObjectIndex, Object, Serial) || Serial != Weak.ObjectSerialNumber ||
            !IsLive(Object) || Object->Index != Weak.ObjectIndex) return nullptr;
        auto* Class = static_cast<SDK::UClass*>(Object);
        return Derives(Class, SDK::AShooterWeapon::StaticClass()) ? Class : nullptr;
    }

    inline SDK::AShooterWeapon* DefaultWeapon(SDK::UClass* Class) {
        if (!Derives(Class, SDK::AShooterWeapon::StaticClass())) return nullptr;
        auto* Default = Class->ClassDefaultObject;
        if (!IsLive(Default, true) || !(Default->Flags & SDK::EObjectFlags::ClassDefaultObject) ||
            Default->Class != Class) return nullptr;
        return static_cast<SDK::AShooterWeapon*>(Default);
    }

    inline bool IsSupported(SDK::AShooterWeapon* Weapon) {
        if (!IsLive(Weapon, true) || !Derives(Weapon->Class, SDK::AShooterWeapon::StaticClass())) return false;
        if (Derives(Weapon->Class, SDK::AShooterWeapon_Instant::StaticClass())) return true;
        auto* Class = Weapon->Class;
        for (unsigned Depth = 0; Class && Depth < 128; ++Depth) {
            if (!IsClass(Class)) return false;
            if (Class->Name.ToString() == "WeapTekPistol_C") return true;
            Class = static_cast<SDK::UClass*>(Class->SuperStruct);
        }
        return false;
    }

    inline bool PositiveTime(SDK::UObject* Object, const char* Name, double& Output) {
        Output = -1;
        if (!IsLive(Object) || !IsClass(Object->Class)) return false;
        auto* Field = Property(Object->Class, Name);
        if (!PropertyBounds(Object, Field, "DoubleProperty", sizeof(double))) return false;
        double Value = 0;
        std::memcpy(&Value, reinterpret_cast<const uint8_t*>(Object) + Field->Offset, sizeof(Value));
        if (!std::isfinite(Value) || Value <= 0) return false;
        Output = Value;
        return true;
    }

    inline bool WorldTime(SDK::UWorld* World, double& Output) {
        Output = -1;
        return IsLive(World) && Derives(World->Class, SDK::UWorld::StaticClass()) &&
            PositiveTime(World, "TimeSeconds", Output);
    }

    inline bool CreationTime(SDK::AShooterWeapon* Weapon, double& Output) {
        Output = -1;
        return IsLive(Weapon) && IsSupported(Weapon) && PositiveTime(Weapon, "CreationTime", Output);
    }

    // Call only for a tuning captured through GetFlags. Its full supported-type
    // check need not be repeated after registry generation and layout checks.
    inline bool CapturedWeapon(SDK::AShooterWeapon* Weapon, SDK::UClass* Class,
        int32_t Index, int32_t Serial, int32_t ClassIndex, int32_t ClassSerial, bool Default) {
        if (!Weapon || !Class || (Default ? ClassSerial <= 0 : Serial <= 0)) return false;
        SDK::UObject* Registered = nullptr;
        int32_t CurrentSerial = -1;
        if (!Registry(Index, Registered, CurrentSerial) || Registered != Weapon ||
            (Serial > 0 && CurrentSerial != Serial) ||
            !Registry(ClassIndex, Registered, CurrentSerial) || Registered != Class ||
            (ClassSerial > 0 && CurrentSerial != ClassSerial) ||
            !Memory(Weapon, sizeof(SDK::UObject)) || !Memory(Class, sizeof(SDK::UClass)) ||
            Weapon->Index != Index || Class->Index != ClassIndex || Weapon->Class != Class) return false;
        const auto Forbidden = SDK::EObjectFlags::BeginDestroyed | SDK::EObjectFlags::FinishDestroyed | SDK::EObjectFlags::MirroredGarbage;
        if ((Weapon->Flags & Forbidden) || (Class->Flags & Forbidden) ||
            (Class->Flags & SDK::EObjectFlags::ClassDefaultObject)) return false;
        const bool IsDefault = (Weapon->Flags & SDK::EObjectFlags::ClassDefaultObject) != 0;
        return IsDefault == Default && (!Default || Class->ClassDefaultObject == Weapon);
    }

    inline bool SingleBit(uint8_t Mask) { return Mask && !(Mask & (Mask - 1)); }
    struct Flag {
        size_t Offset = 0;
        uint8_t Mask = 0;
        ClassShape Owner;
        SDK::FBoolProperty* Descriptor = nullptr;
        SDK::FFieldClass* Kind = nullptr;
        SDK::FName DescriptorName{}, KindName{};
        bool AccessValidated(SDK::UObject* Object, bool Writable = false) const {
            // CapturedWeapon must have just validated this object's generation.
            // Keep fresh page/descriptor/layout guards; skip ancestry/name scans.
            if (!SingleBit(Mask) || !Memory(Object, sizeof(SDK::UObject)) || Object->Class != Owner.Pointer ||
                !Memory(Owner.Pointer, sizeof(SDK::UClass)) || Owner.Pointer->Index != Owner.Index || Owner.Pointer->Size != Owner.Size ||
                Owner.Pointer->Class != Owner.Meta || Owner.Pointer->SuperStruct != Owner.Super ||
                Owner.Pointer->ChildProperties != Owner.Fields || Owner.Pointer->Name != Owner.Name ||
                Offset < sizeof(SDK::UObject) || Owner.Size <= 0 || Offset >= size_t(Owner.Size) ||
                !Memory(Descriptor, sizeof(SDK::FBoolProperty)) || Descriptor->ClassPrivate != Kind ||
                Descriptor->Name != DescriptorName || !Memory(Kind, sizeof(SDK::FFieldClass)) || Kind->Name != KindName ||
                Descriptor->Offset != Offset || Descriptor->ArrayDim != 1 || Descriptor->ElementSize != 1 ||
                Descriptor->FieldSize != 1 || Descriptor->ByteOffset != 0 || Descriptor->FieldMask != Mask || Descriptor->ByteMask != Mask) return false;
            return Memory(reinterpret_cast<uint8_t*>(Object) + Offset, 1, Writable);
        }
        // These two operations require AccessValidated during this same callback.
        bool ReadValidated(SDK::UObject* Object) const {
            return (*(reinterpret_cast<const uint8_t*>(Object) + Offset) & Mask) != 0;
        }
        void WriteValidated(SDK::UObject* Object, bool Value) const {
            auto& Byte = *(reinterpret_cast<uint8_t*>(Object) + Offset);
            Byte = Value ? uint8_t(Byte | Mask) : uint8_t(Byte & ~Mask);
        }
        bool Accessible(SDK::UObject* Object, bool Writable = false) const {
            if (Descriptor) return IsLive(Object, true) && IsClass(Object->Class) && AccessValidated(Object, Writable);
            return SingleBit(Mask) && IsLive(Object, true) && IsClass(Object->Class) &&
                Offset >= sizeof(SDK::UObject) && Object->Class->Size > 0 && Offset < size_t(Object->Class->Size) &&
                Memory(reinterpret_cast<uint8_t*>(Object) + Offset, 1, Writable);
        }
        bool Read(SDK::UObject* Object) const {
            return Accessible(Object) && (*(reinterpret_cast<const uint8_t*>(Object) + Offset) & Mask) != 0;
        }
        void Write(SDK::UObject* Object, bool Value) const {
            if (!Accessible(Object, true)) return;
            auto& Byte = *(reinterpret_cast<uint8_t*>(Object) + Offset);
            Byte = Value ? uint8_t(Byte | Mask) : uint8_t(Byte & ~Mask);
        }
    };

    inline bool GetFlag(SDK::AShooterWeapon* Weapon, const char* Name, Flag& Output) {
        Output = {};
        auto* Field = Property(Weapon->Class, Name);
        if (!PropertyBounds(Weapon, Field, "BoolProperty", 1) || !Memory(Field, sizeof(SDK::FBoolProperty))) return false;
        const auto* Bool = static_cast<SDK::FBoolProperty*>(Field);
        if (Bool->FieldSize != 1 || Bool->ByteOffset != 0 || !SingleBit(Bool->FieldMask) ||
            Bool->ByteMask != Bool->FieldMask) return false;
        Output = {size_t(Field->Offset), Bool->FieldMask};
        if (!ReadClassShape(Weapon->Class, Output.Owner)) return false;
        Output.Descriptor = static_cast<SDK::FBoolProperty*>(Field); Output.Kind = Field->ClassPrivate;
        Output.DescriptorName = Field->Name; Output.KindName = Field->ClassPrivate->Name;
        return Output.Accessible(Weapon, true);
    }

    inline bool GetFlags(SDK::AShooterWeapon* Weapon, Flag& Firing, Flag& Equipping) {
        Firing = {}; Equipping = {};
        if (!IsSupported(Weapon)) return false;
        Flag First{}, Second{};
        if (!GetFlag(Weapon, "bAllowSubmergedFiring", First) ||
            !GetFlag(Weapon, "bPreventEquippingUnderwater", Second) ||
            (First.Offset == Second.Offset && First.Mask == Second.Mask)) return false;
        Firing = First; Equipping = Second;
        return true;
    }
}
