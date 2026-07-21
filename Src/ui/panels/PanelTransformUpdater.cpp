#include "PanelTransformUpdater.h"

#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::panels
{
    namespace
    {
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleRightHandPanelName = "AlwaysVisibleRightHandPanel";
    }

    void PanelTransformUpdater::Update(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        RE::NiNode* handNode,
        RE::NiNode* pinnedAttachNode,
        RE::NiNode* leftHandNode,
        RE::NiNode* rightHandNode,
        bool worldPinned,
        const RE::NiPoint3& handOffset)
    {
        for (const auto& panel : panels) {
            if (!panel || !panel->isActive() || !panel->isShown()) {
                continue;
            }

            if (panel->getName() == kAlwaysVisiblePanelName) {
                if (leftHandNode) {
                    panel->attachToHandNode(leftHandNode, handOffset);
                }
                continue;
            }

            if (panel->getName() == kAlwaysVisibleRightHandPanelName) {
                if (rightHandNode) {
                    panel->attachToHandNode(rightHandNode, handOffset);
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
