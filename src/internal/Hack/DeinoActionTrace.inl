// Called from the verified dinosaur ProcessEvent hook. The callback only compares
// registered pointers and copies bounded parameters; it never calls UE or I/O.
struct DeinoTraceEvent {
    uint64_t At=0;
    const char* Label=nullptr;
    uint32_t NameIndex=0, NameNumber=0;
    int After=0, Ret=-1;
    bool HasVelocity=false;
    double Velocity[3]{};
};
struct DeinoTraceWatch { SDK::UFunction* Function=nullptr; const char* Label=nullptr; int Payload=0; };
struct DeinoActionTraceRuntime {
    std::atomic<bool> Enabled{false};
    std::atomic<uint64_t> EndSequence{0};
    std::mutex Mutex;
    SDK::UObject* Object=nullptr;
    std::array<DeinoTraceWatch,18> Watches{};
    PhoenixDeinoAction::Queue<DeinoTraceEvent,256> Pending;
} DeinoActionTrace;

void ObserveDeinoAction(SDK::UObject* O,SDK::UFunction* F,void* P,bool After) {
    auto& R=DeinoActionTrace;
    if (!R.Enabled.load(std::memory_order_acquire)) return;
    std::scoped_lock Lock(R.Mutex);
    if (!R.Enabled.load(std::memory_order_relaxed) || R.Object!=O) return;
    for (const auto& W:R.Watches) if (W.Function && W.Function==F) {
        DeinoTraceEvent E{GetTickCount64(),W.Label,0,0,int(After),-1};
        if (P && W.Payload==1) { std::memcpy(&E.NameIndex,P,4); std::memcpy(&E.NameNumber,static_cast<uint8_t*>(P)+4,4); }
        if (P && W.Payload==2 && After) E.Ret=*static_cast<uint8_t*>(P)!=0;
        if (P && W.Payload==3) { std::memcpy(E.Velocity,P,24); E.HasVelocity=true; }
        if (W.Payload==4 && After) R.EndSequence.fetch_add(1,std::memory_order_release);
        R.Pending.Push(E); break;
    }
}
void DeinoActionMark(const char* Label) {
    auto& R=DeinoActionTrace;
    if (!R.Enabled.load(std::memory_order_acquire)) return;
    std::scoped_lock Lock(R.Mutex);
    if (R.Enabled.load(std::memory_order_relaxed)) R.Pending.Push({GetTickCount64(),Label});
}
void StopDeinoActionTrace() {
    auto& R=DeinoActionTrace;
    R.Enabled.store(false,std::memory_order_release);
    std::scoped_lock Lock(R.Mutex); R.Object=nullptr; R.Watches={};
}
void ArmDeinoActionTrace(SDK::APrimalDinoCharacter* D) {
    struct Contract { const char* Name; int Size,Payload; };
    constexpr Contract Contracts[]={
        {"ServerStartUberpounceFromFalling",120,0},{"StartUberpounceFromFalling",72,0},
        {"ServerEndUberpounce",32,0},{"MultiEndUberpounce",32,4},{"EndUberpounce",1088,0},
        {"CanContinueUberpounce",441,2},{"BlueprintAnimNotifyCustomEvent",425,1},
        {"ServerRequestJump",24,3},{"ApplyJump",36,3},
        {"LatchedJumpStart",0,0},{"Latched Jump Launched",51,0},{"ServerRequestLatchedAutoJump",24,3},
        {"BP_OnJumpPressed",8,0},{"BP_OnJumpReleased",105,0},{"ClientStartUberpounce",116,0},
        {"ServerRequestUberpounce",73,0},{"ServerRequestLatchedJump",24,3},{"JumpFromUberpounce",320,0}};
    std::array<DeinoTraceWatch,18> Watches{};
    static_assert(std::size(Contracts)==std::tuple_size_v<decltype(Watches)>);
    for (size_t I=0;I<Watches.size();++I) {
        const auto& C=Contracts[I]; auto* F=D->Class->GetFunction("Deinonychus_Character_BP_C",C.Name);
        Watches[I].Label=C.Name;
        if (!IsUsableObject(F) || F->Size!=C.Size) continue;
        if (C.Payload==1 && !NativeMountParam(F,"CustomEventName","NameProperty",0,8)) continue;
        if (C.Payload==2 && !NativeMountParam(F,"Ret","BoolProperty",0,1)) continue;
        if (C.Payload==3 && !NativeMountParam(F,"Velocity","StructProperty",0,24)) continue;
        Watches[I]={F,C.Name,C.Payload};
    }
    auto& R=DeinoActionTrace;
    std::scoped_lock Lock(R.Mutex); R.Pending.Clear(); R.Object=D; R.Watches=Watches; R.EndSequence.store(0,std::memory_order_release);
    R.Enabled.store(true,std::memory_order_release);
}
void DrainDeinoActionTrace(std::ofstream& File,uint64_t Start) {
    PhoenixDeinoAction::Queue<DeinoTraceEvent,256> Events;
    { std::scoped_lock Lock(DeinoActionTrace.Mutex); Events=DeinoActionTrace.Pending; DeinoActionTrace.Pending.Clear(); }
    for (size_t I=0;I<Events.Count;++I) {
        const auto& E=Events.Items[I];
        File<<"# event,"<<(E.At>=Start ? E.At-Start : 0)<<','<<E.Label<<','<<E.After<<','<<E.Ret;
        if (E.NameIndex) File<<','<<SDK::FName(static_cast<int32_t>(E.NameIndex),E.NameNumber).ToString();
        if (E.HasVelocity) File<<",velocity,"<<E.Velocity[0]<<','<<E.Velocity[1]<<','<<E.Velocity[2];
        File<<'\n';
    }
    if (Events.Dropped) File<<"# dropped_events="<<Events.Dropped<<'\n';
}
