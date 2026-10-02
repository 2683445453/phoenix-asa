// Game-thread local root rotation. Character movement retains ownership of networking.
// No camera writes, acceleration, server rotation RPCs or persistent flag patches.
struct DeinoHoverFacingResult {
    int Status=0; // 1 applied, 2 contract failure, 3 invalid values, 4 rotation rejected
    double Before=0,After=0,Requested=0;
};
DeinoHoverFacingResult ApplyDeinoHoverFacing(SDK::APrimalDinoCharacter* D,
    SDK::UCharacterMovementComponent* M,double ViewYaw) {
    DeinoHoverFacingResult Result;
    if(!IsUsableObject(D) || !IsUsableObject(M) || D->CharacterMovement!=M) { Result.Status=2; return Result; }
    auto* Get=NativeMountFunction(D,"Actor","K2_GetActorRotation",24);
    auto* Set=NativeMountFunction(D,"Actor","K2_SetActorRotation",32);
    auto* Desired=NativeMountFunction(D,"PrimalDinoCharacter","SetLastMovementDesiredRotation",24);
    if(!NativeMountParam(Get,"ReturnValue","StructProperty",0,24) ||
        !NativeMountParam(Set,"NewRotation","StructProperty",0,24) ||
        !NativeMountParam(Set,"bTeleportPhysics","BoolProperty",24,1) ||
        !NativeMountParam(Set,"ReturnValue","BoolProperty",25,1) ||
        !NativeMountParam(Desired,"InRotation","StructProperty",0,24)) { Result.Status=2; return Result; }
    SDK::FRotator Rotation{};
    D->ProcessEvent(Get,&Rotation);
    const auto Velocity=M->Velocity;
    if(!std::isfinite(ViewYaw) || !std::isfinite(Rotation.Pitch) || !std::isfinite(Rotation.Yaw) ||
        !std::isfinite(Rotation.Roll) || !std::isfinite(Velocity.X) || !std::isfinite(Velocity.Y) ||
        !std::isfinite(Velocity.Z)) { Result.Status=3; return Result; }
    Result.Before=Result.After=Rotation.Yaw;
    Result.Requested=Rotation.Yaw=std::remainder(ViewYaw,360.0);
    struct { SDK::FRotator Rotation; bool Teleport=true,Applied=false; } Args{Rotation};
    static_assert(sizeof(Args)==32 && offsetof(decltype(Args),Applied)==25);
    D->ProcessEvent(Set,&Args);
    M->Velocity=Velocity;
    if(!Args.Applied) { Result.Status=4; return Result; }
    D->ProcessEvent(Desired,&Rotation);
    D->ProcessEvent(Get,&Rotation);
    M->Velocity=Velocity;
    Result.After=Rotation.Yaw;
    Result.Status=std::isfinite(Result.After) && std::abs(std::remainder(Result.After-Result.Requested,360.0))<1?1:4;
    return Result;
}
