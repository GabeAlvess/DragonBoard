#include "PanelTransformUpdater.h"

#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::panels
{
    namespace
    {
        constexpr const char* kAlwaysVisibleHmdPanelName = "AlwaysVisibleHmdPanel";
    }

    void PanelTransformUpdater::Update(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        RE::NiNode* handNode,
        RE::NiNode* pinnedAttachNode,
        RE::NiNode* headNode,
        bool worldPinned,
        const RE::NiPoint3& handOffset)
    {
        for (const auto& panel : panels) {
            if (!panel || !panel->isActive() || !panel->isShown()) {
                continue;
            }

            if (panel->getName() == kAlwaysVisibleHmdPanelName) {
                if (headNode) {
                    panel->attachToNode(headNode);
                    panel->setLocalPosition({ 0.0f, 0.0f, 0.0f });
                }
                continue;
            }

            if (worldPinned && pinnedAttachNode) {
                panel->attachToNode(pinnedAttachNode);
            } else if (handNode) {
                panel->attachToHandNode(handNode, handOffset);
            }
        }
    }
}
