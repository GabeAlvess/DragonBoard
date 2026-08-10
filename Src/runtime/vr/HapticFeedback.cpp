#include "runtime/vr/HapticFeedback.h"

#include <RE/B/BSOpenVR.h>

#include <algorithm>
#include <atomic>
#include <chrono>

namespace dragonboard::runtime::vr
{
    namespace
    {
        constexpr auto kSlowCallThreshold = std::chrono::milliseconds(25);
        constexpr float kMaximumEffectiveDurationSeconds = 0.05f;
        std::atomic_bool hapticsDisabled{ false };
    }

    void TriggerHaptic(
        bool isDominantHand,
        bool useLeftHandAsMenu,
        bool nativeLeftHandedMode,
        float intensity,
        float duration)
    {
#ifdef ENABLE_SKYRIM_VR
        if (hapticsDisabled.load(std::memory_order_acquire)) return;

        auto* openVR = RE::BSOpenVR::GetSingleton();
        if (!openVR) return;

        const bool physicalRightController =
            useLeftHandAsMenu ? isDominantHand : !isDominantHand;
        const bool rightController =
            physicalRightController != nativeLeftHandedMode;
        const float effectiveSeconds =
            (std::min)(
                std::clamp(duration, 0.0f, 0.25f) *
                    std::clamp(intensity, 0.0f, 1.0f),
                kMaximumEffectiveDurationSeconds);
        if (effectiveSeconds <= 0.0f) return;

        const auto startedAt = std::chrono::steady_clock::now();
        openVR->TriggerHapticPulse(rightController, effectiveSeconds * 250.0f);
        const auto elapsed = std::chrono::steady_clock::now() - startedAt;
        if (elapsed >= kSlowCallThreshold) {
            hapticsDisabled.store(true, std::memory_order_release);
            logger::warn(
                "DragonBoardVR: Skyrim OpenVR haptic call blocked for {:.3f} ms; "
                "haptics disabled for this session.",
                std::chrono::duration<double, std::milli>(elapsed).count());
        }
#endif
    }
}
