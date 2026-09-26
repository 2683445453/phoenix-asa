#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// 带「原值记录 + 恢复」的反射字段补丁器（秒换弹 / 弓秒蓄力共用）。
//
// 与纯规则层（InstantReloadRules.h）分开，原因有二：
//   1. 规则层要能脱离 SDK 做离线单测，本文件只使用 SDK 类型；
//   2. 与项目既有 FeatureExpansionPatch.inl（规则/补丁分离）保持一致。
// 与 FeatureExpansionPatch.inl 相同，本文件不包含 SDK.hpp：包含它的翻译单元
// 必须已经引入 SDK 类型（Hack.cpp 经 Configs.h 引入；离线测试自带最小 SDK 桩）。
namespace PhoenixReload {
    struct NestedFieldPatch {
        // 对象身份校验回调（由运行时注入 TryGetLiveObjectSerial + IsUsableObject）：
        // 「指针 + 索引 + 序列号」三合一校验，防止 GC 槽位复用后把原值写回新对象。
        // 为空时退化为「指针未变且索引未变」，仍然不放行明显的槽位复用。
        using AliveCheck = bool (*)(const SDK::UObject*, int, int*);
        AliveCheck Alive = nullptr;

        SDK::UObject* Owner = nullptr;
        int OwnerIndex = -1;
        int OwnerSerial = 0;
        size_t Offset = 0;
        uint8_t Mask = 0;
        bool IsBool = false;
        float Original = 0, Applied = 0;
        bool Active = false;
        size_t FieldSize = 0;

        // 字段名/类型名比对。生产实现走 FName::ToString()（需要游戏名称表）；
        // 离线回归无法初始化名称表，因此做成 virtual 以便测试替换比对方式。
        virtual bool MatchesFieldName(const SDK::FName& Name, const char* Expected) const {
            return Name.ToString() == Expected;
        }
        virtual bool MatchesClassName(const SDK::FFieldClass* FieldClass, const char* Expected) const {
            return FieldClass && FieldClass->Name.ToString() == Expected;
        }
        virtual ~NestedFieldPatch() = default;

        bool SameOwner(SDK::UObject* Object) const {
            return Object && Object == Owner && OwnerIsLive();
        }
        bool OwnerIsLive() const {
            if (!Owner) return false;
            int Serial = 0;
            // Consult the object registry before dereferencing a potentially freed owner.
            if (Alive && (!Alive(Owner, OwnerIndex, &Serial) || Serial != OwnerSerial)) return false;
            return int(Owner->Index) == OwnerIndex;
        }
        void Clear() {
            Owner = nullptr; OwnerIndex = -1; OwnerSerial = 0; Offset = 0; Mask = 0;
            IsBool = false; Original = Applied = 0; Active = false; FieldSize = 0;
        }
        void Restore() {
            if (Active && OwnerIsLive()) {
                auto* Field = reinterpret_cast<uint8_t*>(Owner) + Offset;
                if (IsBool) {
                    // 只有当前位仍等于我们写入的值时才恢复，避免覆盖游戏本体的新状态。
                    const uint8_t Current = *Field;
                    if (((Current & Mask) != 0) == (Applied != 0))
                        *Field = Original != 0 ? uint8_t(Current | Mask) : uint8_t(Current & ~Mask);
                }
                else if (*reinterpret_cast<float*>(Field) == Applied) {
                    *reinterpret_cast<float*>(Field) = Original;
                }
            }
            Clear();
        }

        // 定位目标字段：
        //   StructName == nullptr → 类上的顶层 FloatProperty
        //   StructName != nullptr → 类上 StructName 结构体内的 BoolProperty
        // 所有偏移都按「内层落在结构体内、整体落在实例内」双重校验。
        uint8_t Locate(SDK::UObject* Object, const char* StructName, const char* FieldName,
            size_t& OutOffset, size_t& OutFieldSize,
            SDK::FStructProperty*& OutStruct, bool& OutIsBool) {
            OutOffset = 0; OutFieldSize = 0; OutStruct = nullptr; OutIsBool = false;
            if (!Object || !Object->Class || !FieldName) return 0;
            if (Object->IsDefaultObject()) return 0;
            auto* ObjectClass = Object->Class;
            for (auto* Type = ObjectClass; Type; Type = static_cast<SDK::UClass*>(Type->SuperStruct)) {
                for (auto* FieldIt = Type->ChildProperties; FieldIt; FieldIt = FieldIt->Next) {
                    auto* Field = static_cast<SDK::FProperty*>(FieldIt);
                    if (!Field->ClassPrivate) continue;
                    if (!MatchesFieldName(Field->Name, StructName ? StructName : FieldName)) continue;
                    if (Field->Offset < 0 || Field->ArrayDim != 1 || Field->ElementSize <= 0 ||
                        Field->Offset > ObjectClass->Size - Field->ElementSize) return 0;
                    if (!StructName) {
                        if (!MatchesClassName(Field->ClassPrivate, "FloatProperty") ||
                            Field->ElementSize != int(sizeof(float))) return 0;
                        OutOffset = size_t(Field->Offset);
                        OutFieldSize = size_t(Field->ElementSize);
                        return 1;
                    }
                    if (!MatchesClassName(Field->ClassPrivate, "StructProperty")) return 0;
                    auto* Struct = static_cast<SDK::FStructProperty*>(Field);
                    if (!Struct->Struct || Struct->Struct->Size <= 0 ||
                        Struct->Struct->Size > Field->ElementSize) return 0;
                    for (auto* InnerIt = Struct->Struct->ChildProperties; InnerIt; InnerIt = InnerIt->Next) {
                        auto* Inner = static_cast<SDK::FProperty*>(InnerIt);
                        if (!Inner->ClassPrivate) continue;
                        if (!MatchesFieldName(Inner->Name, FieldName)) continue;
                        if (Inner->Offset < 0 || Inner->ArrayDim != 1 || Inner->ElementSize <= 0 ||
                            Inner->Offset > Struct->Struct->Size - Inner->ElementSize) return 0;
                        if (MatchesClassName(Inner->ClassPrivate, "FloatProperty")) {
                            if (Inner->ElementSize != int(sizeof(float))) return 0;
                            OutOffset = size_t(Field->Offset) + size_t(Inner->Offset);
                            OutFieldSize = sizeof(float);
                            OutStruct = Struct;
                            return 1;
                        }
                        if (!MatchesClassName(Inner->ClassPrivate, "BoolProperty") ||
                            Inner->Offset < 0 || Inner->ArrayDim != 1 || Inner->ElementSize != 1)
                            return 0;
                        auto* Bool = static_cast<SDK::FBoolProperty*>(Inner);
                        const uint8_t Mask = Bool->FieldMask ? Bool->FieldMask : Bool->ByteMask;
                        if (!Mask || Bool->ByteOffset >= 8) return 0;
                        // ByteOffset 是相对结构体起点的位段字节偏移。
                        const size_t InnerOffset = size_t(Bool->Offset) + Bool->ByteOffset;
                        if (InnerOffset >= size_t(Struct->Struct->Size)) return 0;
                        const size_t Combined = size_t(Field->Offset) + InnerOffset;
                        if (Combined >= size_t(ObjectClass->Size)) return 0;
                        OutOffset = Combined;
                        OutFieldSize = 1;
                        OutStruct = Struct;
                        OutIsBool = true;
                        return Mask;
                    }
                    return 0;
                }
            }
            return 0;
        }

        // 位段型 bool：原值、应用值与恢复都按位处理。
        void ApplyBool(SDK::UObject* Object, const char* StructName, const char* FieldName, bool Value) {
            size_t NewOffset = 0, NewSize = 0;
            SDK::FStructProperty* Struct = nullptr;
            bool NewIsBool = false;
            const uint8_t NewMask = Locate(Object, StructName, FieldName, NewOffset, NewSize,
                Struct, NewIsBool);
            if (!NewMask || !NewIsBool) { Restore(); return; }
            if (Active && (!SameOwner(Object) || Offset != NewOffset || Mask != NewMask)) Restore();
            if (!Active) {
                Owner = Object; OwnerIndex = int(Object->Index);
                if (Alive && !Alive(Owner, OwnerIndex, &OwnerSerial)) { Clear(); return; }
                Offset = NewOffset; Mask = NewMask; FieldSize = NewSize; IsBool = true;
                Original = Applied = (*(reinterpret_cast<uint8_t*>(Object) + Offset) & Mask) ? 1.0f : 0.0f;
                Active = true;
            }
            auto* Field = reinterpret_cast<uint8_t*>(Object) + Offset;
            const bool Current = (*Field & Mask) != 0;
            if (Current != (Applied != 0)) Original = Current ? 1.0f : 0.0f; // 外部改写成为新基线
            *Field = Value ? uint8_t(*Field | Mask) : uint8_t(*Field & ~Mask);
            Applied = Value ? 1.0f : 0.0f;
        }

        // 浮点字段（顶层或结构体内）：原值与应用值都是 float。
        void ApplyNumber(SDK::UObject* Object, const char* StructName, const char* FieldName, float Value) {
            size_t NewOffset = 0, NewSize = 0;
            SDK::FStructProperty* Struct = nullptr;
            bool NewIsBool = false;
            Locate(Object, StructName, FieldName, NewOffset, NewSize, Struct, NewIsBool);
            if (NewSize != sizeof(float) || NewIsBool) { Restore(); return; }
            if (Active && (!SameOwner(Object) || Offset != NewOffset)) Restore();
            if (!Active) {
                Owner = Object; OwnerIndex = int(Object->Index);
                if (Alive && !Alive(Owner, OwnerIndex, &OwnerSerial)) { Clear(); return; }
                Offset = NewOffset; Mask = 0; FieldSize = NewSize; IsBool = false;
                Original = Applied = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(Object) + Offset);
                Active = true;
            }
            auto* Field = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(Object) + Offset);
            if (*Field != Applied) Original = *Field;
            *Field = Applied = Value;
        }
    };
}
