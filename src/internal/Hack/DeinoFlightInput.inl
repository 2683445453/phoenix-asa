// The game tick publishes weak identities. The hook only checks identities;
// it never calls game functions or writes files inside ProcessEvent.
struct DeinoFlightInputRuntime {
    std::atomic<SDK::UObject*> Target{nullptr};
    std::mutex Mutex;
    ExtensionObject Dino,Movement,Pressed,Released;
    uint64_t Until=0;
    bool GroundPress=false;
} DeinoFlightInput;
void ClearDeinoFlightInput() {
    DeinoFlightInput.Target.store(nullptr,std::memory_order_release);
    std::scoped_lock Lock(DeinoFlightInput.Mutex);
    DeinoFlightInput.Dino={}; DeinoFlightInput.Movement={}; DeinoFlightInput.Pressed={}; DeinoFlightInput.Released={}; DeinoFlightInput.Until=0; DeinoFlightInput.GroundPress=false;
}
bool SetDeinoFlightInput(SDK::APrimalDinoCharacter* D,SDK::UCharacterMovementComponent* M,bool GroundPress=false) {
    auto* Press=AirLatchFunction(D,"BP_OnJumpPressed",8);
    auto* Release=AirLatchFunction(D,"BP_OnJumpReleased",105);
    if (!Press || !NativeMountParam(Release,"HoldTime","DoubleProperty",0,8) ||
        !g_Hook::DinoActionEventsOK ||
        (*reinterpret_cast<void***>(D))[SDK::Offsets::ProcessEventIdx]!=g_Hook::DinoActionEventTarget) return false;
    const auto* A=AirLatchScript(Press); const auto* B=AirLatchScript(Release);
    const uint8_t Ret[]={4,0x0b,0x53};
    if (A->Num()!=106 || B->Num()!=231 || !A->GetDataPtr() || !B->GetDataPtr() ||
        std::memcmp(A->GetDataPtr()+103,Ret,3) || std::memcmp(B->GetDataPtr()+228,Ret,3)) return false;
    std::scoped_lock Lock(DeinoFlightInput.Mutex);
    DeinoFlightInput.Dino=ExtensionObject(D); DeinoFlightInput.Pressed=ExtensionObject(Press);
    DeinoFlightInput.Movement=ExtensionObject(M);
    DeinoFlightInput.GroundPress=GroundPress;
    DeinoFlightInput.Released=ExtensionObject(Release); DeinoFlightInput.Until=GetTickCount64()+150;
    DeinoFlightInput.Target.store(D,std::memory_order_release);
    return true;
}
bool SuppressFlightInput(SDK::UObject* D,SDK::UFunction* F) {
    if (!D || DeinoFlightInput.Target.load(std::memory_order_acquire)!=D) return false;
    std::scoped_lock Lock(DeinoFlightInput.Mutex);
    auto* M=static_cast<SDK::UCharacterMovementComponent*>(DeinoFlightInput.Movement.Get());
    // Recheck live movement mode so a press immediately after landing remains native.
    return M && (M->MovementMode==SDK::EMovementMode::MOVE_Falling || M->MovementMode==SDK::EMovementMode::MOVE_None ||
        (DeinoFlightInput.GroundPress && (M->MovementMode==SDK::EMovementMode::MOVE_Walking || M->MovementMode==SDK::EMovementMode::MOVE_NavWalking))) &&
        GetTickCount64()<=DeinoFlightInput.Until && DeinoFlightInput.Dino.Get()==D &&
        (DeinoFlightInput.Pressed.Get()==F || DeinoFlightInput.Released.Get()==F);
}
