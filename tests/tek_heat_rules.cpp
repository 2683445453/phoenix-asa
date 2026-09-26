#include "../src/internal/Hack/TekHeatRules.h"
#include <array>
#include <cassert>
#include <iostream>
int main() {
    // Captured CL 829680 instruction shapes with a fixture FName index.
    std::array<uint8_t,17> Rifle{0x1b,7,0xf6,0xea,0,0,0,0,0,0,0,0,0,0x16,4,0x0b,0x53};
    std::array<uint8_t,18> Sniper{0x1b,7,0xf6,0xea,0,0,0,0,0,0,0,0,0,0x27,0x16,4,0x0b,0x53};
    assert(PhoenixTek::IsHeatOnlyFireEvent(Rifle,0xeaf607,false));
    assert(PhoenixTek::IsHeatOnlyFireEvent(Sniper,0xeaf607,true));
    assert(!PhoenixTek::IsHeatOnlyFireEvent(Sniper,0xeaf607,false));
    assert(!PhoenixTek::IsHeatOnlyFireEvent(Rifle,0xeaf608,false));
    for(size_t N=0;N<Rifle.size();++N)assert(!PhoenixTek::IsHeatOnlyFireEvent({Rifle.data(),N},0xeaf607,false));
    // Every byte is part of the verified call/parameter/return contract.
    for(size_t I=0;I<Rifle.size();++I){auto Changed=Rifle;Changed[I]^=0xff;assert(!PhoenixTek::IsHeatOnlyFireEvent(Changed,0xeaf607,false));}
    Sniper[13]=0x28;assert(!PhoenixTek::IsHeatOnlyFireEvent(Sniper,0xeaf607,true));
    std::cout<<"Tek heat-only event verification rejects different names, parameters, bodies and truncated bytecode.\n";
}
