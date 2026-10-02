#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace PhoenixRespawn {
    static_assert(sizeof(void*) == 8, "ASA respawn dispatch requires x64.");
    // Verified ASA execClientNotifyRespawned wrapper: UE parses Pawn and bool
    // before dispatching the native virtual implementation. Never detour this
    // wrapper with the native (Controller, Pawn, bool) calling convention.
    inline std::optional<std::size_t> NativeSlot(std::span<const std::uint8_t> Wrapper) {
        constexpr std::array<std::uint8_t, 11> Prologue{
            0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x56};
        constexpr std::array<std::uint8_t, 31> Tail{
            0x48, 0x8b, 0x43, 0x20, 0x49, 0x8b, 0xce,
            0x48, 0x8b, 0x54, 0x24, 0x48, 0x48, 0x85, 0xc0,
            0x40, 0x0f, 0x95, 0xc7, 0x48, 0x03, 0xf8,
            0x83, 0x7c, 0x24, 0x40, 0x00, 0x48, 0x89, 0x7b, 0x20};
        constexpr std::array<std::uint8_t, 13> Call{
            0x49, 0x8b, 0x06, 0x41, 0x0f, 0x95, 0xc0,
            0xff, 0x90, 0xc8, 0x17, 0x00, 0x00};
        if (Wrapper.size() < 0x17b ||
            std::memcmp(Wrapper.data(), Prologue.data(), Prologue.size()) != 0 ||
            std::memcmp(Wrapper.data() + 0x148, Tail.data(), Tail.size()) != 0 ||
            std::memcmp(Wrapper.data() + 0x167, Call.data(), Call.size()) != 0)
            return std::nullopt;
        return 0x17c8 / sizeof(void*);
    }

    template<class Controller, class Pawn, class Original>
    void Dispatch(Controller* PC, Pawn* NewPawn, bool IsFirstSpawn,
        bool Enabled, bool Bypass, Original Native) {
        // First download must retain character initialization and UI cleanup.
        // Preserve the existing shortcut only for ordinary respawns.
        if (Enabled && !Bypass && !IsFirstSpawn && PC && NewPawn) return;
        Native(PC, NewPawn, IsFirstSpawn);
    }
}
