#pragma once

#include <RE/N/NiPoint3.h>

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
    class PanelTransformUpdater
    {
    public:
        static void Update(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            RE::NiNode* handNode,
            RE::NiNode* pinnedAttachNode,
            RE::NiNode* headNode,
            bool worldPinned,
            const RE::NiPoint3& handOffset);
    };
}
