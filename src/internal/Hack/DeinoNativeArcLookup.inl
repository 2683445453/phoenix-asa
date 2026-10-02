// GetFullName uses FName::ToString, which strips the package path in this SDK.
// Compare the raw outer name so another package with the same leaf cannot match.
bool IsDeinoNativeArcTemplate(SDK::UObject* O) {
    return IsUsableObject(O) && O->GetName()=="NS_TekGrenadeLauncherProjArcBeam" &&
        O->Class->GetName()=="NiagaraSystem" &&
        IsUsableObject(O->Outer) && !O->Outer->Outer &&
        O->Outer->Class->GetName()=="Package" &&
        O->Outer->Name.GetRawString()=="/Game/Genesis/Effects/Particles/Weapons/TekGrenadeLauncher/NS_TekGrenadeLauncherProjArcBeam";
}
SDK::UFunction* DeinoArcClassFunction(SDK::UClass* Type,const char* Owner,const char* Name,int Size) {
    if(!IsUsableObject(Type)) return nullptr;
    // Libraries execute on their CDO. The general mount helper rejects CDOs.
    auto* CDO=Type->ClassDefaultObject;
    int32_t Serial=-1;
    if(!CDO || CDO->Class!=Type || !CDO->IsDefaultObject() ||
        !TryGetLiveObjectSerial(CDO,CDO->Index,&Serial) ||
        (CDO->Flags & SDK::EObjectFlags::BeginDestroyed) ||
        (CDO->Flags & SDK::EObjectFlags::FinishDestroyed) ||
        (CDO->Flags & SDK::EObjectFlags::MirroredGarbage)) return nullptr;
    auto* F=Type->GetFunction(Owner,Name);
    if(!IsUsableObject(F) || F->Size!=Size ||
        !(static_cast<uint32_t>(F->FunctionFlags)&0x400) ||
        (static_cast<uint32_t>(F->FunctionFlags)&0x40)) return nullptr;
    return F;
}
