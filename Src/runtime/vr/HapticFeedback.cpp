#include "runtime/vr/HapticFeedback.h"

#include <RE/B/BSOpenVR.h>

namespace dragonboard::runtime::vr
{
    void TriggerHaptic(
        bool isDominantHand,
        bool useLeftHandAsMenu,
        bool nativeLeftHandedMode,
        float intensity,
        float duration)
    {
#ifdef ENABLE_SKYRIM_VR
        auto* openVR = RE::BSOpenVR::GetSingleton();
        if (!openVR) return;

        const bool physicalRightController =
            useLeftHandAsMenu ? isDominantHand : !isDominantHand;
        const bool rightController =
            physicalRightController != nativeLeftHandedMode;
        const float gameDuration = duration * 250.0f * intensity;
        if (gameDuration > 0.0f) {
            openVR->TriggerHapticPulse(rightController, gameDuration);
        }
#endif
    }
}
