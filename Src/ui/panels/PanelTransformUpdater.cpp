#include "PanelTransformUpdater.h"

#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::ui::panels
{
    namespace
    {
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kFixedWidgetsContainerName = "FixedWidgetsContainer";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleRightHandPanelName = "AlwaysVisibleRightHandPanel";

        void ApplyFixedWidgetScaleCompensation(
            vrui::VRUIPanel& panel,
            bool physicalBoardActive)
        {
            if (panel.getName() != kPersistentPanelName) return;
            float scale = 1.0f;
            if (physicalBoardActive) {
                const auto& settings = vrui::VRUISettings::get();
                const float physicalScale = settings.physicalBoardScale > 0.001f ?
                    settings.physicalBoardScale : 1.0f;
                scale = settings.menuScale / physicalScale;
            }
            if (auto* fixedWidgets = panel.findWidgetByName(kFixedWidgetsContainerName)) {
                fixedWidgets->setLocalScale(scale);
            }
        }
    }

    void PanelTransformUpdater::Update(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        RE::NiNode* physicalAnchor,
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

            ApplyFixedWidgetScaleCompensation(*panel, physicalAnchor != nullptr);

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

            if (physicalAnchor) {
                panel->attachToPhysicalNode(physicalAnchor);
            } else if (worldPinned && pinnedAttachNode) {
                panel->attachToNode(pinnedAttachNode);
            } else if (handNode) {
                panel->attachToHandNode(handNode, handOffset);
            }
        }
    }
}