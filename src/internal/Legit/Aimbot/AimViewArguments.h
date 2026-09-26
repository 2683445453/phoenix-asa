#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace g_Aimbot {
    inline uintptr_t ResolveSpreadGetterTarget(std::span<const uint8_t> Code, uintptr_t Wrapper) {
        // Verified ShooterWeapon_Instant.GetCurrentSpread wrapper: a direct
        // native call followed by writing the scalar XMM0 result to the caller.
        constexpr uint8_t FloatReturn[] = {0xf3,0x0f,0x11,0x03,0x48,0x83,0xc4,0x20,0x5b,0xc3};
        if (Code.size() < 0x2d || Code[0x1e] != 0xe8 ||
            std::memcmp(Code.data() + 0x23, FloatReturn, sizeof(FloatReturn)) != 0) return 0;
        int32_t Relative = 0;
        std::memcpy(&Relative, Code.data() + 0x1f, sizeof(Relative));
        return static_cast<uintptr_t>(static_cast<intptr_t>(Wrapper + 0x23) + Relative);
    }

    struct ViewArgumentRefs {
        uintptr_t Location = 0, Direction = 0;
    };

    inline bool ReadViewArgumentRefs(std::span<const uint8_t> Script, size_t Offset, ViewArgumentRefs& Refs) {
        // Two EX_LocalVariable expressions, each followed by an FProperty*.
        // Only this verified form is writable; other expressions pass through.
        constexpr size_t ExpressionSize = 1 + sizeof(uintptr_t);
        if (Offset > Script.size() || Script.size() - Offset < ExpressionSize * 2 ||
            Script[Offset] != 0x00 || Script[Offset + ExpressionSize] != 0x00) return false;
        ViewArgumentRefs Candidate{};
        std::memcpy(&Candidate.Location, Script.data() + Offset + 1, sizeof(uintptr_t));
        std::memcpy(&Candidate.Direction, Script.data() + Offset + ExpressionSize + 1, sizeof(uintptr_t));
        if (!Candidate.Location || !Candidate.Direction || Candidate.Location == Candidate.Direction) return false;
        Refs = Candidate;
        return true;
    }

    inline bool ValidViewVectorOffset(int32_t Offset, int32_t ElementSize, int32_t ArrayDim, int32_t FrameSize) {
        return FrameSize >= 24 && Offset >= 0 && Offset % 8 == 0 &&
            ElementSize == 24 && ArrayDim == 1 && Offset <= FrameSize - ElementSize;
    }

    template<typename Vector>
    bool RedirectViewFromOrigin(const Vector& Target, const Vector& Origin, Vector& Direction) {
        const double X = Target.X - Origin.X, Y = Target.Y - Origin.Y, Z = Target.Z - Origin.Z;
        const double Length = std::hypot(X, Y, Z);
        if (!std::isfinite(Length) || Length <= 1.0) return false;
        Direction = {X / Length, Y / Length, Z / Length};
        return true;
    }
}
