// Restrict the experiment to the current player's preview and placement call.
namespace {
    struct PlacementFlagLayout { int Offset = -1; uint8_t Mask = 0; } PlacementRequestFlag, PlacementCheckFlag;
    ExtensionObject PlacementPC, PlacementPreview;
    std::atomic<SDK::UFunction*> PlacementRequestEvent{nullptr}, PlacementCheckEvent{nullptr};
    std::atomic<uint64_t> PlacementChecks{0}, PlacementRequests{0};
    std::string PlacementStatus = "浮空建筑实验未启用";
    PlacementFlagLayout FindPlacementFlag(SDK::UFunction* F, const char* Parameter) {
        PlacementFlagLayout Out;
        auto* P = IsUsableObject(F) ? FindReflectedProperty(F, Parameter) : nullptr;
        if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString() != "StructProperty" ||
            P->ArrayDim != 1 || P->Offset < 0 || P->ElementSize <= 0 ||
            P->ElementSize > F->Size || P->Offset > F->Size - P->ElementSize) return Out;
        auto* S = static_cast<SDK::FStructProperty*>(P)->Struct;
        auto* B = S && S->GetName() == "PlacementData" ? FindReflectedProperty(S,"bIgnoreGroundRequirement") : nullptr;
        if (!B || !B->ClassPrivate || B->ClassPrivate->Name.ToString() != "BoolProperty" || B->ArrayDim != 1 || B->Offset < 0) return Out;
        auto* BP = static_cast<SDK::FBoolProperty*>(B);
        if (BP->ByteOffset >= 8 || B->Offset + BP->ByteOffset >= P->ElementSize) return Out;
        Out.Offset = P->Offset + B->Offset + BP->ByteOffset;
        Out.Mask = BP->FieldMask ? BP->FieldMask : BP->ByteMask;
        return Out;
    }
}
std::string GetFloatingStructureStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(PlacementStatus); }
void ResetPlacementExperiment() {
    PlacementRequestEvent.store(nullptr); PlacementCheckEvent.store(nullptr);
    PlacementPC = {}; PlacementPreview = {};
}
void PlacementExperimentTick(SDK::UWorld* World) {
    auto* PC = GetLocalShooterController(World);
    auto* Placer = IsUsableObject(PC) ? ReadReflectedObjectValue(PC,"StructurePlacer") : nullptr;
    auto* Preview = IsUsableObject(Placer) ? ReadReflectedObjectValue(Placer,"CurrentPlacingStructure") : nullptr;
    DWORD PID = 0; GetWindowThreadProcessId(GetForegroundWindow(),&PID);
    if (!g_Config::bFloatingStructures || !IsUsableObject(Preview) || IsFreeCameraActive() ||
        PID != GetCurrentProcessId() || !PhoenixShortcuts::GameplayInputReady(PC,g_MDX12::g_MenuState::g_isOpen,false)) {
        PlacementRequestEvent.store(nullptr); PlacementCheckEvent.store(nullptr); PlacementPC = {}; PlacementPreview = {};
        ExtensionStatus(PlacementStatus,g_Config::bFloatingStructures ? "手持建筑并进入放置预览后尝试；服务器仍会检查支撑" : "浮空建筑实验未启用");
        return;
    }
    if (PlacementPC.Get() != PC || PlacementPreview.Get() != Preview) {
        PlacementRequestEvent.store(nullptr); PlacementCheckEvent.store(nullptr);
        PlacementPC = ExtensionObject(PC); PlacementPreview = ExtensionObject(Preview);
        auto* Request = PC->Class->GetFunction("ShooterPlayerController","ServerRequestPlaceStructure");
        auto* Check = Preview->Class->GetFunction("PrimalStructure","IsAllowedToBuild");
        PlacementRequestFlag = FindPlacementFlag(Request,"PlacementData");
        PlacementCheckFlag = FindPlacementFlag(Check,"OutPlacementData");
        if (PlacementRequestFlag.Offset >= 0 && PlacementRequestFlag.Mask) PlacementRequestEvent.store(Request);
        if (PlacementCheckFlag.Offset >= 0 && PlacementCheckFlag.Mask) PlacementCheckEvent.store(Check);
    }
    ExtensionStatus(PlacementStatus,FormatLocalized("忽略地面条件实验：预览 {} 次，放置请求 {} 次；是否成功以服务器为准",PlacementChecks.load(),PlacementRequests.load()));
}
bool HandlePlacementExperiment(SDK::UObject* Object, SDK::UFunction* F, void* Args, NativeGameplayEvent Original) {
    if (!g_Config::bFloatingStructures || !F || !Args) return false;
    const bool Request = F == PlacementRequestEvent.load();
    if (!Request && F != PlacementCheckEvent.load()) return false;
    if (Object != (Request ? PlacementPC.Get() : PlacementPreview.Get())) return false;
    const auto Layout = Request ? PlacementRequestFlag : PlacementCheckFlag;
    if (Layout.Offset < 0 || Layout.Offset >= F->Size || !Layout.Mask) return false;
    auto* Byte = static_cast<uint8_t*>(Args) + Layout.Offset;
    const uint8_t Old = *Byte & Layout.Mask;
    *Byte |= Layout.Mask;
    if (Request) PlacementRequests.fetch_add(1); else PlacementChecks.fetch_add(1);
    Original(Object,F,Args);
    // Parameter buffers belong to the engine. Retain native changes to other bits.
    *Byte = uint8_t((*Byte & ~Layout.Mask) | Old);
    return true;
}
