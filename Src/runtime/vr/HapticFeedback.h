#pragma once

namespace dragonboard::runtime::vr
{
    void TriggerHaptic(
        bool isDominantHand,
        bool useLeftHandAsMenu,
        float intensity,
        float duration);
}
