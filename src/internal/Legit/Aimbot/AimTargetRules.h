#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>

namespace g_Aimbot {
    inline bool IsShotgunAssetName(std::string_view Name) {
        return Name == "WeapShotgun_C" || Name == "WeapMachinedShotgun_C";
    }

    enum class AimBodyPart { None, Head, Body };

    inline bool AimPartEnabled(AimBodyPart Part, bool Head, bool Body) {
        return (Part == AimBodyPart::Head && Head) || (Part == AimBodyPart::Body && Body);
    }

    inline bool WithinAimRange(double CameraDistance, double ShotDistance,
        double ConfiguredRange, double WeaponRange) {
        return std::isfinite(CameraDistance) && std::isfinite(ShotDistance) &&
            std::isfinite(ConfiguredRange) && ConfiguredRange > 0 &&
            CameraDistance > 1 && CameraDistance <= ConfiguredRange && ShotDistance > 1 &&
            WeaponRange > 0 && ShotDistance <= WeaponRange;
    }

    inline bool AcceptAimHit(bool BlockingHit, bool HitTarget, AimBodyPart SelectedPart,
        AimBodyPart HitPart, bool HasHitBone, bool Head, bool Body) {
        if (!BlockingHit || !HitTarget || !AimPartEnabled(SelectedPart, Head, Body)) return false;
        // A capsule without a bone can verify a body fallback, never a headshot.
        // With both enabled, a head in front of the torso is still an allowed
        // hit. With only one enabled the other part remains excluded.
        return HasHitBone ? AimPartEnabled(HitPart, Head, Body) : SelectedPart == AimBodyPart::Body;
    }

    inline std::string AimBoneLower(std::string_view Name) {
        std::string Lower(Name);
        for (auto& Ch : Lower) if (Ch >= 'A' && Ch <= 'Z') Ch += 'a' - 'A';
        return Lower;
    }
    inline bool ExcludedAimBone(std::string_view Lower) {
        // Reject appendages before matching torso/head words (e.g. tail_spine).
        for (const auto Excluded : {"tail", "wing", "feather", "finger", "claw", "foot", "toe",
            "leg", "arm", "hand", "jaw", "horn", "spike", "socket", "neck", "weapon", "saddle",
            "flipper", "tentacle", "antenna", "lft_back", "rht_back"})
            if (Lower.find(Excluded) != std::string::npos) return true;
        const auto Fin = Lower.find("fin");
        if (Fin != std::string::npos && (Fin + 3 == Lower.size() || Lower[Fin + 3] == '_' ||
            (Lower[Fin + 3] >= '0' && Lower[Fin + 3] <= '9'))) return true;
        return false;
    }
    inline AimBodyPart ClassifyAimBone(std::string_view Name) {
        const auto Lower = AimBoneLower(Name);
        if (ExcludedAimBone(Lower)) return AimBodyPart::None;
        for (const auto Head : {"head", "skull", "cranium", "cephalon"})
            if (Lower.find(Head) != std::string::npos) return AimBodyPart::Head;
        // Giga/therizino/dodo use c_back. Raptor/stego Lft_Back/Rht_Back
        // are hind legs, so "back" must never be a general substring match.
        const auto Stem = [&](std::string_view Prefix) {
            if (!Lower.starts_with(Prefix)) return false;
            if (Lower.size() == Prefix.size()) return true;
            const char Next = Lower[Prefix.size()];
            return Next == '_' || (Next >= '0' && Next <= '9');
        };
        if (Stem("c_back") || Stem("cnt_back") || Stem("back") || Stem("hip") || Stem("c_hip"))
            return AimBodyPart::Body;
        for (const auto Body : {"spine", "pelvis", "hips", "chest", "torso", "body", "abdomen", "thorax", "carapace", "shell"})
            if (Lower.find(Body) != std::string::npos) return AimBodyPart::Body;
        return AimBodyPart::None;
    }

    inline AimBodyPart ClassifyAimPhysicsBone(std::string_view Name, bool SolePhysicsBody) {
        const auto Part = ClassifyAimBone(Name);
        if (Part != AimBodyPart::None || !SolePhysicsBody) return Part;
        const auto Lower = AimBoneLower(Name);
        // Some small creatures have one whole-body collider on a generic bone.
        // This exception requires actual geometry and never promotes appendages.
        return !Lower.empty() && Lower != "none" && !ExcludedAimBone(Lower)
            ? AimBodyPart::Body : AimBodyPart::None;
    }

    struct TargetHoldState {
        uint64_t Weapon = 0, Target = 0;
        bool Active = false;

        bool Update(bool Enabled, bool ButtonDown, uint64_t WeaponId) {
            if (!Enabled || !ButtonDown || !WeaponId) { *this = {}; return false; }
            const bool Started = !Active || Weapon != WeaponId;
            if (Started) { Weapon = WeaponId; Target = 0; Active = true; }
            return Started;
        }
        void Capture(uint64_t TargetId) { if (Active && !Target && TargetId) Target = TargetId; }
        bool Allows(uint64_t TargetId) const { return !Active || !Target || Target == TargetId; }
    };
}
