#pragma once

namespace dragonboard::runtime::vr
{
    void TriggerHaptic(
        bool isDominantHand,
        bool useLeftHandAsMenu,
        bool nativeLeftHandedMode,
        float intensity,
        float duration);
}
