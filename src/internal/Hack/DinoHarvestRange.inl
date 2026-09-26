// Included inside g_Hack, after WeaponWaterAccess.inl. Call only from the
// serialized game-thread harvest callback; the caller owns riding/alive/UI gates.
namespace DinoHarvestRange {
    constexpr int MaximumAttacks = 64, MaximumSockets = 64;
    constexpr float MaximumRadiusUU = 2000.0f;
    using Attack = SDK::FDinoAttackInfo;
    struct RawArray { uint8_t* Data=nullptr; int32_t Num=0, Max=0; };
    struct View { Attack* Data=nullptr; int32_t Num=0, Max=0; size_t BasicOffset=0; uint8_t BasicMask=0; };
    struct Key {
        SDK::FName Name{};
        std::vector<SDK::FName> Sockets;
        bool operator==(const Key& Other) const { return Name==Other.Name && Sockets==Other.Sockets; }
    };
    struct Patch { Key Identity; float Original=0, Applied=0; bool Owned=false; };
    struct Owner {
        SDK::APrimalDinoCharacter* Pointer=nullptr;
        SDK::UClass* Class=nullptr;
        int32_t Index=-1, Serial=-1, ClassIndex=-1, ClassSerial=-1;
        bool Capture(SDK::APrimalDinoCharacter* Dino) {
            SDK::UObject* Registered=nullptr;
            if (!WaterAccess::IsLive(Dino) || !WaterAccess::Derives(Dino->Class,SDK::APrimalDinoCharacter::StaticClass()) ||
                !WaterAccess::Registry(Dino->Index,Registered,Serial) || Registered!=Dino || Serial<=0 ||
                !WaterAccess::Registry(Dino->Class->Index,Registered,ClassSerial) || Registered!=Dino->Class) return false;
            Pointer=Dino; Class=Dino->Class; Index=Dino->Index; ClassIndex=Class->Index; return true;
        }
        bool Matches(SDK::APrimalDinoCharacter* Dino) const {
            SDK::UObject* Registered=nullptr; int32_t Current=-1;
            return Dino && Dino==Pointer && WaterAccess::Registry(Index,Registered,Current) && Registered==Dino && Current==Serial &&
                WaterAccess::IsLive(Dino) && Dino->Class==Class &&
                WaterAccess::Registry(ClassIndex,Registered,Current) && Registered==Class &&
                (ClassSerial<=0 || ClassSerial==Current) && WaterAccess::Derives(Class,SDK::APrimalDinoCharacter::StaticClass());
        }
    };
    Owner CurrentOwner;
    std::vector<Patch> Patches;
    uint64_t NextUpdate=0;
    float LastMultiplier=0;
    std::mutex StatusMutex;
    std::string Status="普通龙本地近战范围未启用";
    void SetStatus(const char* Text) { std::scoped_lock Lock(StatusMutex); if (Status!=Text) Status=Text; }

    bool Kind(SDK::FProperty* Field,const char* Name,size_t Size,int32_t Offset) {
        return WaterAccess::Memory(Field,sizeof(SDK::FProperty)) &&
            WaterAccess::Memory(Field->ClassPrivate,sizeof(SDK::FFieldClass)) &&
            Field->ClassPrivate->Name.ToString()==Name && Field->ArrayDim==1 &&
            Field->ElementSize==Size && Field->Offset==Offset;
    }
    bool Ordinary(SDK::APrimalDinoCharacter* Dino) {
        if (!WaterAccess::IsLive(Dino) || !WaterAccess::Derives(Dino->Class,SDK::APrimalDinoCharacter::StaticClass())) return false;
        auto* Class=Dino->Class;
        for (unsigned Depth=0;Class && Depth<128;++Depth) {
            if (!WaterAccess::IsClass(Class)) return false;
            auto Name=Class->Name.ToString();
            std::transform(Name.begin(),Name.end(),Name.begin(),[](unsigned char C){return char(std::tolower(C));});
            if (Name.find("tekstridercharacterbp")!=std::string::npos) return false;
            Class=static_cast<SDK::UClass*>(Class->SuperStruct);
        }
        return Class==nullptr;
    }
    bool ReadView(SDK::APrimalDinoCharacter* Dino,View& Out) {
        Out={};
        if (!Ordinary(Dino)) return false;
        auto* Field=WaterAccess::Property(Dino->Class,"AttackInfos");
        if (!WaterAccess::PropertyBounds(Dino,Field,"ArrayProperty",sizeof(RawArray)) ||
            !WaterAccess::Memory(Field,sizeof(SDK::FArrayProperty))) return false;
        auto* Inner=static_cast<SDK::FArrayProperty*>(Field)->InnerProperty;
        if (!WaterAccess::Memory(Inner,sizeof(SDK::FStructProperty)) ||
            !Kind(Inner,"StructProperty",sizeof(Attack),0)) return false;
        auto* Struct=static_cast<SDK::FStructProperty*>(Inner)->Struct;
        if (!WaterAccess::IsLive(Struct) || !WaterAccess::Memory(Struct,sizeof(SDK::UStruct)) ||
            !WaterAccess::Derives(Struct->Class,SDK::UScriptStruct::StaticClass()) ||
            Struct->Name.ToString()!="DinoAttackInfo" || Struct->Size!=sizeof(Attack)) return false;
        // Match each native field against this build's real SDK before reading
        // any array element. The mutable radius is the sole field we write.
        unsigned Found=0;
        auto* Member=Struct->ChildProperties;
        for (unsigned Count=0;Member && Count<256;++Count) {
            if (!WaterAccess::Memory(Member,sizeof(SDK::FProperty))) return false;
            auto* Property=static_cast<SDK::FProperty*>(Member);
            const auto Name=Property->Name.ToString();
            if (Name=="MeleeSwingRadius") {
                if (!Kind(Property,"FloatProperty",sizeof(float),offsetof(Attack,MeleeSwingRadius))) return false;
                Found|=1;
            } else if (Name=="AttackName") {
                if (!Kind(Property,"NameProperty",sizeof(SDK::FName),offsetof(Attack,AttackName))) return false;
                Found|=2;
            } else if (Name=="MeleeDamageAmount") {
                if (!Kind(Property,"IntProperty",sizeof(int32_t),offsetof(Attack,MeleeDamageAmount))) return false;
                Found|=4;
            } else if (Name=="ProjectileClass") {
                if (!Kind(Property,"ClassProperty",sizeof(void*),offsetof(Attack,ProjectileClass))) return false;
                Found|=8;
            } else if (Name=="MeleeSwingSockets") {
                if (!Kind(Property,"ArrayProperty",sizeof(RawArray),offsetof(Attack,MeleeSwingSockets)) ||
                    !WaterAccess::Memory(Property,sizeof(SDK::FArrayProperty))) return false;
                auto* Element=static_cast<SDK::FArrayProperty*>(Property)->InnerProperty;
                if (!Kind(Element,"NameProperty",sizeof(SDK::FName),0)) return false;
                Found|=16;
            } else if (Name=="bBasicAttack") {
                if (!WaterAccess::Memory(Property,sizeof(SDK::FBoolProperty)) ||
                    !WaterAccess::Memory(Property->ClassPrivate,sizeof(SDK::FFieldClass)) ||
                    Property->ClassPrivate->Name.ToString()!="BoolProperty" || Property->ArrayDim!=1 ||
                    Property->ElementSize!=1 || Property->Offset<0 || Property->Offset>=sizeof(Attack)) return false;
                const auto* Bit=static_cast<SDK::FBoolProperty*>(Property);
                if (Bit->FieldSize!=1 || Bit->ByteOffset!=0 || !WaterAccess::SingleBit(Bit->FieldMask) || Bit->ByteMask!=Bit->FieldMask) return false;
                Out.BasicOffset=size_t(Property->Offset); Out.BasicMask=Bit->FieldMask; Found|=32;
            }
            Member=Member->Next;
        }
        if (Member || Found!=63) return false;
        RawArray Array{};
        std::memcpy(&Array,reinterpret_cast<const uint8_t*>(Dino)+Field->Offset,sizeof(Array));
        if (Array.Num<0 || Array.Num>MaximumAttacks || Array.Max<Array.Num || Array.Max>MaximumAttacks*4 ||
            (Array.Num && !WaterAccess::Memory(Array.Data,size_t(Array.Num)*sizeof(Attack),true))) return false;
        Out.Data=reinterpret_cast<Attack*>(Array.Data); Out.Num=Array.Num; Out.Max=Array.Max; return true;
    }
    bool ReadKey(const View& View,int Index,Key& Out) {
        if (Index<0 || Index>=View.Num) return false;
        const auto& Attack=View.Data[Index];
        // Identity remains meaningful when attack eligibility changes. It is
        // also used to restore a formerly basic attack without modifying its
        // new flags, projectile or damage settings.
        RawArray Sockets{}; std::memcpy(&Sockets,&Attack.MeleeSwingSockets,sizeof(Sockets));
        if (Sockets.Num<=0 || Sockets.Num>MaximumSockets || Sockets.Max<Sockets.Num || Sockets.Max>MaximumSockets*4 ||
            !WaterAccess::Memory(Sockets.Data,size_t(Sockets.Num)*sizeof(SDK::FName))) return false;
        Out.Name=Attack.AttackName;
        const auto* Names=reinterpret_cast<const SDK::FName*>(Sockets.Data);
        Out.Sockets.assign(Names,Names+Sockets.Num); return true;
    }
    bool Eligible(const View& View,int Index) {
        if (Index<0 || Index>=View.Num) return false;
        const auto& Attack=View.Data[Index];
        const auto* Bytes=reinterpret_cast<const uint8_t*>(&Attack);
        SDK::UObject* Projectile=nullptr;
        std::memcpy(&Projectile,&Attack.ProjectileClass,sizeof(Projectile));
        return (Bytes[View.BasicOffset]&View.BasicMask) && !Projectile && Attack.MeleeDamageAmount>0 &&
            std::isfinite(Attack.MeleeSwingRadius) && Attack.MeleeSwingRadius>0;
    }
    struct Candidate { int Index=-1; Key Identity; };
    std::vector<Candidate> ReadCandidates(const View& View) {
        std::vector<Candidate> Result;
        Result.reserve(size_t(View.Num));
        for (int Index=0;Index<View.Num;++Index) {
            Key Identity;
            if (ReadKey(View,Index,Identity)) Result.push_back({Index,std::move(Identity)});
        }
        return Result;
    }
    int UniqueIndex(const std::vector<Candidate>& Candidates,const Key& Identity) {
        int Found=-1;
        for (const auto& Candidate:Candidates) {
            if (!(Candidate.Identity==Identity)) continue;
            if (Found>=0) return -1;
            Found=Candidate.Index;
        }
        return Found;
    }
    void Restore() {
        View Current;
        if (CurrentOwner.Matches(CurrentOwner.Pointer) && ReadView(CurrentOwner.Pointer,Current)) {
            const auto Candidates=ReadCandidates(Current);
            for (auto& Patch:Patches) {
                // Resolve against the current validated array. Never reuse an
                // old allocation after a resize, reorder or destroyed owner.
                const int Index=UniqueIndex(Candidates,Patch.Identity);
                if (Index<0 || !Patch.Owned) continue;
                auto& Radius=Current.Data[Index].MeleeSwingRadius;
                if (Radius==Patch.Applied) Radius=Patch.Original;
            }
        }
        Patches.clear(); CurrentOwner={}; NextUpdate=0; LastMultiplier=0;
    }
}

void ResetDinoHarvestRange() {
    WaterAccess::ValidationPass Validation;
    DinoHarvestRange::Restore();
    DinoHarvestRange::SetStatus("普通龙本地近战范围未启用");
}
std::string GetDinoHarvestRangeStatus() {
    std::scoped_lock Lock(DinoHarvestRange::StatusMutex);
    return LanguageManager::Get().TranslateCopy(DinoHarvestRange::Status);
}
void UpdateDinoHarvestRange(SDK::APrimalDinoCharacter* Dino,bool Enabled,float Multiplier,uint64_t Now) {
    WaterAccess::ValidationPass Validation;
    using namespace DinoHarvestRange;
    if (!Enabled || !Dino) { Restore(); SetStatus("普通龙本地近战范围未启用"); return; }
    if (!CurrentOwner.Matches(Dino)) {
        Restore();
        if (!Ordinary(Dino) || !CurrentOwner.Capture(Dino)) {
            SetStatus("未找到可验证的当前普通龙，已跳过范围修改"); return;
        }
    }
    const float Scale=std::clamp(std::isfinite(Multiplier)?Multiplier:2.0f,1.0f,5.0f);
    if (Now<NextUpdate && Scale==LastMultiplier) return;
    NextUpdate=Now+250; LastMultiplier=Scale;
    View Current;
    if (!ReadView(Dino,Current)) { Restore(); SetStatus("近战数组或字段未通过验证，已停止修改"); return; }
    const auto Candidates=ReadCandidates(Current);
    // Restore verified former candidates before dropping their ownership. A
    // mutable eligibility flag is not proof that the old radius is no longer ours.
    for (const auto& Previous:Patches) {
        const int Index=UniqueIndex(Candidates,Previous.Identity);
        if (Index<0 || Eligible(Current,Index) || !Previous.Owned) continue;
        auto& Radius=Current.Data[Index].MeleeSwingRadius;
        if (Radius==Previous.Applied) Radius=Previous.Original;
    }
    std::vector<Patch> Desired;
    for (const auto& Candidate:Candidates) {
        const int Index=Candidate.Index;
        const auto& Identity=Candidate.Identity;
        if (!Eligible(Current,Index) || UniqueIndex(Candidates,Identity)!=Index) continue;
        auto Existing=std::find_if(Patches.begin(),Patches.end(),[&](const auto& P){return P.Identity==Identity;});
        Patch Next=Existing==Patches.end()?Patch{Identity}:*Existing;
        auto& Radius=Current.Data[Index].MeleeSwingRadius;
        if (!Next.Owned || Radius!=Next.Applied) Next.Original=Radius;
        const float Value=std::max(Next.Original,std::min(Next.Original*Scale,MaximumRadiusUU));
        Next.Applied=Value; Next.Owned=true;
        if (Radius!=Value) Radius=Value;
        Desired.push_back(std::move(Next));
    }
    Patches=std::move(Desired);
    SetStatus(Patches.empty()?"没有可验证的普通近战攻击项，未修改范围":
        "已调整当前普通龙的本地近战半径；实际采集与命中由服务器确认");
}
bool GetDinoHarvestReachMeters(SDK::APrimalDinoCharacter* Dino,float& OutMeters) {
    WaterAccess::ValidationPass Validation;
    using namespace DinoHarvestRange;
    OutMeters=0; View Current;
    if (!ReadView(Dino,Current)) return false;
    float Smallest=std::numeric_limits<float>::max();
    const auto Candidates=ReadCandidates(Current);
    for (const auto& Candidate:Candidates) {
        if (Eligible(Current,Candidate.Index) && UniqueIndex(Candidates,Candidate.Identity)==Candidate.Index)
            Smallest=std::min(Smallest,Current.Data[Candidate.Index].MeleeSwingRadius);
    }
    if (Smallest==std::numeric_limits<float>::max()) return false;
    OutMeters=Smallest*.01f; return std::isfinite(OutMeters) && OutMeters>0;
}
