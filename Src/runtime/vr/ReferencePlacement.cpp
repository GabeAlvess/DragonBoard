#include "runtime/vr/ReferencePlacement.h"

#include <REL/Relocation.h>
#include <RE/T/TESObjectREFR.h>

namespace dragonboard::runtime::vr
{
    namespace
    {
        // Skyrim VR 1.4.15 offsets from CommonLibSSE-NG's VR address map.
        constexpr std::size_t kSetAngleOffset = 0x02A7C50;
        constexpr std::size_t kSetPositionOffset = 0x02A8010;
    }

    bool SetReferenceTransform(
        RE::TESObjectREFR* reference,
        const RE::NiPoint3& position,
        const RE::NiPoint3& angle)
    {
        if (!reference) {
            return false;
        }

        using SetPosition = void(RE::TESObjectREFR*, const RE::NiPoint3&);
        using SetAngle = void(RE::TESObjectREFR*, const RE::NiPoint3&);
        static REL::Relocation<SetPosition> setPosition{ REL::Offset(kSetPositionOffset) };
        static REL::Relocation<SetAngle> setAngle{ REL::Offset(kSetAngleOffset) };

        setPosition(reference, position);
        setAngle(reference, angle);
        return true;
    }
}
