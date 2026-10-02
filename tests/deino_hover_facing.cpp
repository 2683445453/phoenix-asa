// Exercise the production root-rotation helper without a running game.
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
namespace SDK {
struct FVector { double X=0,Y=0,Z=0; };
struct FRotator { double Pitch=0,Yaw=0,Roll=0; };
struct UFunction { std::string Name; int Size; };
struct UObject { bool Live=true; };
struct UCharacterMovementComponent : UObject { FVector Velocity; };
struct APrimalDinoCharacter : UObject {
    UCharacterMovementComponent* CharacterMovement=nullptr;
    FRotator Rotation{12,170,-3},Desired{};
    bool AllowSet=true,CorrectRotation=false;
    int Sets=0,DesiredSets=0;
    void ProcessEvent(UFunction* F,void* P) {
        if(F->Name=="K2_GetActorRotation") *static_cast<FRotator*>(P)=Rotation;
        else if(F->Name=="K2_SetActorRotation") {
            ++Sets;
            assert(*reinterpret_cast<bool*>(static_cast<char*>(P)+24));
            if(AllowSet) { Rotation=*static_cast<FRotator*>(P); CharacterMovement->Velocity={99,98,97}; }
            *reinterpret_cast<bool*>(static_cast<char*>(P)+25)=AllowSet;
        } else {
            assert(F->Name=="SetLastMovementDesiredRotation");
            ++DesiredSets; Desired=*static_cast<FRotator*>(P);
            if(CorrectRotation) Rotation.Yaw+=20;
        }
    }
};
}
SDK::UFunction GetRotation{"K2_GetActorRotation",24},SetRotation{"K2_SetActorRotation",32},SetDesired{"SetLastMovementDesiredRotation",24};
bool GoodContract=true;
bool IsUsableObject(SDK::UObject* O) { return O && O->Live; }
SDK::UFunction* NativeMountFunction(SDK::UObject* O,const char*,const char* Name,int Size) {
    if(!IsUsableObject(O)) return nullptr;
    for(auto* F:{&GetRotation,&SetRotation,&SetDesired}) if(F->Name==Name && F->Size==Size) return F;
    return nullptr;
}
bool NativeMountParam(SDK::UFunction* F,const char* Name,const char* Type,int Offset,int Size) {
    if(!F || !GoodContract) return false;
    const std::string N=Name,T=Type;
    if(F==&GetRotation) return N=="ReturnValue" && T=="StructProperty" && Offset==0 && Size==24;
    if(F==&SetDesired) return N=="InRotation" && T=="StructProperty" && Offset==0 && Size==24;
    return (N=="NewRotation" && T=="StructProperty" && Offset==0 && Size==24) ||
        (N=="bTeleportPhysics" && T=="BoolProperty" && Offset==24 && Size==1) ||
        (N=="ReturnValue" && T=="BoolProperty" && Offset==25 && Size==1);
}
#include "../src/internal/Hack/DeinoHoverFacing.inl"
int main() {
    SDK::UCharacterMovementComponent Move; Move.Velocity={1,2,-.5};
    SDK::APrimalDinoCharacter Dino; Dino.CharacterMovement=&Move;
    for(double Yaw:{-180.0,-179.0,0.0,90.0,179.0,180.0,540.0,721.0}) {
        auto R=ApplyDeinoHoverFacing(&Dino,&Move,Yaw);
        assert(R.Status==1 && std::abs(std::remainder(Dino.Rotation.Yaw-Yaw,360.0))<1e-9);
        assert(Dino.Rotation.Pitch==12 && Dino.Rotation.Roll==-3);
        assert(Dino.Desired.Yaw==Dino.Rotation.Yaw && Dino.Desired.Pitch==12);
        assert(Move.Velocity.X==1 && Move.Velocity.Y==2 && Move.Velocity.Z==-.5);
    }
    auto Sets=Dino.Sets;
    GoodContract=false;
    assert(ApplyDeinoHoverFacing(&Dino,&Move,0).Status==2 && Dino.Sets==Sets); GoodContract=true;
    assert(ApplyDeinoHoverFacing(&Dino,&Move,NAN).Status==3 && Dino.Sets==Sets);
    Move.Live=false;
    assert(ApplyDeinoHoverFacing(&Dino,&Move,0).Status==2 && Dino.Sets==Sets); Move.Live=true;
    SDK::UCharacterMovementComponent Other;
    assert(ApplyDeinoHoverFacing(&Dino,&Other,0).Status==2 && Dino.Sets==Sets);
    Dino.Rotation.Pitch=NAN;
    assert(ApplyDeinoHoverFacing(&Dino,&Move,0).Status==3 && Dino.Sets==Sets); Dino.Rotation.Pitch=12;
    Dino.AllowSet=false; const auto DesiredSets=Dino.DesiredSets;
    assert(ApplyDeinoHoverFacing(&Dino,&Move,0).Status==4 && Dino.DesiredSets==DesiredSets);
    Dino.AllowSet=true; Dino.CorrectRotation=true;
    assert(ApplyDeinoHoverFacing(&Dino,&Move,0).Status==4);
    std::cout<<"Hover facing: yaw wrap, preserved tilt/velocity, identity/contracts, invalid values and rejected/corrected turns passed.\n";
}
