#pragma once

#include <RE/N/NiPoint3.h>

namespace RE
{
    class TESObjectREFR;
}

namespace dragonboard::runtime::vr
{
    [[nodiscard]] bool SetReferenceTransform(
        RE::TESObjectREFR* reference,
        const RE::NiPoint3& position,
        const RE::NiPoint3& angle);
}
