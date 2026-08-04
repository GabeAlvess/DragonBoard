#pragma once

#include <cstdint>

namespace dragonboard::integrations::vrik
{
    void Initialize();
    void InitializeHiggsHandCollisionSuppression();
    void SetHolsterSlotSuppressed(std::int32_t slotIndex, bool suppressed);
    void ApplyTouchPointingPose(bool leftHand);
    void RestoreTouchHandPose();
}
