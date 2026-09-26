void UpdateSessionSnapshot(SDK::UWorld* World) {
    const auto Now=GetTickCount64();
    if (Now>PhoenixSession::RequestedUntil.load()) return;
    static ExtensionObject PreviousWorld, PreviousHuman;
    static uint64_t Generation=0, Next=0;
    if (Now<Next) return;
    Next=Now+100;
    PhoenixSession::Snapshot S;
    S.Updated=Now;
    auto* PC=GetLocalShooterController(World);
    auto* Human=IsUsableObject(PC)?PC->GetPlayerCharacter():nullptr;
    if (!IsUsableObject(World) || !IsUsableObject(PC) || !IsUsableObject(Human) || Human->IsDead()) {
        PreviousWorld={}; PreviousHuman={}; PhoenixSession::Publish(S); return;
    }
    if (PreviousWorld.Get()!=World || PreviousHuman.Get()!=Human) {
        ++Generation; PreviousWorld=ExtensionObject(World); PreviousHuman=ExtensionObject(Human);
    }
    S.Session=Generation;
    auto* Root=Human->RootComponent;
    if (!IsUsableObject(Root)) { PhoenixSession::Publish(S); return; }
    // RelativeLocation of a root is in world space unless attached; ask the
    // checked native accessor to include moving-platform attachments.
    auto* LocationFn=Human->Class->GetFunction("Actor","K2_GetActorLocation");
    if (!LocationFn) { PhoenixSession::Publish(S); return; }
    auto P=Human->K2_GetActorLocation();
    S.X=P.X*.01; S.Y=P.Y*.01; S.Z=P.Z*.01;
    S.Valid=std::isfinite(S.X)&&std::isfinite(S.Y)&&std::isfinite(S.Z);
    DWORD ForegroundPID=0;GetWindowThreadProcessId(GetForegroundWindow(),&ForegroundPID);
    S.InputReady=S.Valid && ForegroundPID==GetCurrentProcessId() &&
        PhoenixShortcuts::GameplayInputReady(PC,g_MDX12::g_MenuState::g_isOpen,false);
    if (auto* Status=Human->MyCharacterStatusComponent; IsUsableObject(Status)) {
        const auto Ratio=[&](int Index) {
            float V=Status->CurrentStatusValues[Index], M=Status->MaxStatusValues[Index];
            return std::isfinite(V)&&std::isfinite(M)&&M>0 ? std::clamp(V/M,0.f,10.f) : -1.f;
        };
        S.Health=Ratio(0); S.Oxygen=Ratio(3); S.Weight=Ratio(7);
    }
    PhoenixSession::Publish(S);
}
