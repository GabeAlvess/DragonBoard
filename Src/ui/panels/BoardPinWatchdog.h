#pragma once

#include <memory>
#include <vector>

namespace RE
{
    class NiNode;
}

namespace vrui
{
    class VRUIPanel;
}

namespace dragonboard::ui::panels
{
    class BoardPinState;

    class BoardPinWatchdog
    {
    public:
        static bool ShouldReturnToHand(
            BoardPinState& state,
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            RE::NiNode* menuHandNode,
            float deltaTime,
            bool verboseLogging,
            float maxDistance = 500.0f);
    };
}
