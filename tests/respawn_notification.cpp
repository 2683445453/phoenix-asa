#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/RespawnNotification.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
    struct Pawn {};
    struct Controller { int Notifications = 0; bool SpawnMenuOpen = true; };
    Controller* ExpectedPC = nullptr;
    Pawn* ExpectedPawn = nullptr;
    bool ExpectedFirst = false;
    void __fastcall Native(Controller* PC, Pawn* NewPawn, bool First) {
        assert(PC == ExpectedPC && NewPawn == ExpectedPawn && First == ExpectedFirst);
        if (PC) { ++PC->Notifications; PC->SpawnMenuOpen = false; }
    }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream File(argv[1]);
    assert(File);
    std::vector<std::uint8_t> Wrapper;
    unsigned Byte = 0;
    while (File >> std::hex >> Byte) {
        assert(Byte <= 255);
        Wrapper.push_back(static_cast<std::uint8_t>(Byte));
    }
    assert(File.eof());
    const auto Slot = PhoenixRespawn::NativeSlot(Wrapper);
    assert(Slot && *Slot == 761);
    for (std::size_t Size = 0; Size < 0x17b; ++Size)
        assert(!PhoenixRespawn::NativeSlot({Wrapper.data(), Size}));
    // Reject changed argument decoding/virtual dispatch rather than guessing
    // a slot after an engine update. Fixture comes from the installed client.
    for (auto [Offset, Size] : {std::pair{0u, 11u}, {0x148u, 31u}, {0x167u, 13u}}) {
        for (unsigned i = Offset; i < Offset + Size; ++i) {
            auto Changed = Wrapper; Changed[i] ^= 1;
            assert(!PhoenixRespawn::NativeSlot(Changed));
        }
    }
    Controller PC; Pawn Character;
    ExpectedPC = &PC; ExpectedPawn = &Character;
    // Repeated first-spawn notifications (including transfer retry) must
    // retain native UI cleanup even with the shortcut enabled.
    ExpectedFirst = true;
    for (int i = 0; i < 3; ++i) {
        PC.SpawnMenuOpen = true;
        PhoenixRespawn::Dispatch(&PC, &Character, true, true, false, Native);
        assert(!PC.SpawnMenuOpen && PC.Notifications == i + 1);
    }
    ExpectedFirst = false; PC.SpawnMenuOpen = true;
    PhoenixRespawn::Dispatch(&PC, &Character, false, true, false, Native);
    assert(PC.Notifications == 3); // existing ordinary respawn shortcut
    PhoenixRespawn::Dispatch(&PC, &Character, false, false, false, Native);
    assert(PC.Notifications == 4 && !PC.SpawnMenuOpen);
    PC.SpawnMenuOpen = true;
    PhoenixRespawn::Dispatch(&PC, &Character, false, true, true, Native);
    assert(PC.Notifications == 5 && !PC.SpawnMenuOpen); // unload forwards
    ExpectedPawn = nullptr;
    PhoenixRespawn::Dispatch(&PC, static_cast<Pawn*>(nullptr), false, true, false, Native);
    assert(PC.Notifications == 6); // incomplete notification is not discarded
    std::cout << "Respawn notification ABI, first-spawn forwarding and wrapper guards passed.\n";
}
