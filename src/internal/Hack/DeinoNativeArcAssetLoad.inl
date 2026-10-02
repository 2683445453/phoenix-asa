// BPLoadObject in this client only finds resident objects. Use the engine's
// synchronous soft-reference loader so an unequipped weapon is not required.
// Called only on the game thread; returns 1 loaded, 2 missing, 3 bad contract.
int LoadDeinoNativeArcAsset(SDK::UObject*& Result) {
    Result=nullptr;
    auto* Type=SDK::UKismetSystemLibrary::StaticClass();
    auto* NameType=SDK::UKismetStringLibrary::StaticClass();
    auto* Load=DeinoArcClassFunction(Type,"KismetSystemLibrary","LoadAsset_Blocking",48);
    auto* Name=DeinoArcClassFunction(NameType,"KismetStringLibrary","Conv_StringToName",24);
    if(!NativeMountParam(Load,"Asset","SoftObjectProperty",0,40) ||
        !NativeMountParam(Load,"ReturnValue","ObjectProperty",40,8) ||
        !NativeMountParam(Name,"InString","StrProperty",0,16) ||
        !NativeMountParam(Name,"ReturnValue","NameProperty",16,8)) return 3;
    auto ToName=[&](const wchar_t* Text) {
        struct { SDK::FString Text; SDK::FName Value{}; } Args{SDK::FString(Text)};
        static_assert(sizeof(Args)==24 && offsetof(decltype(Args),Value)==16);
        NameType->ClassDefaultObject->ProcessEvent(Name,&Args);
        return Args.Value;
    };
    struct LoadArgs {
        SDK::TSoftObjectPtr<SDK::UObject> Asset{};
        SDK::UObject* ReturnValue=nullptr;
    } Args;
    static_assert(sizeof(LoadArgs)==48 && offsetof(LoadArgs,ReturnValue)==40);
    // Two interned names and an empty subobject path avoid owning engine strings.
    // The zero-initialized weak reference must not point at a previously loaded asset.
    auto& Path=Args.Asset.ObjectID.AssetPath;
    Path.PackageName=ToName(L"/Game/Genesis/Effects/Particles/Weapons/TekGrenadeLauncher/NS_TekGrenadeLauncherProjArcBeam");
    Path.AssetName=ToName(L"NS_TekGrenadeLauncherProjArcBeam");
    if(Path.PackageName.GetRawString()!= "/Game/Genesis/Effects/Particles/Weapons/TekGrenadeLauncher/NS_TekGrenadeLauncherProjArcBeam" ||
        Path.AssetName.GetRawString()!= "NS_TekGrenadeLauncherProjArcBeam") return 3;
    Type->ClassDefaultObject->ProcessEvent(Load,&Args);
    if(!IsDeinoNativeArcTemplate(Args.ReturnValue)) return 2;
    Result=Args.ReturnValue;
    return 1;
}
