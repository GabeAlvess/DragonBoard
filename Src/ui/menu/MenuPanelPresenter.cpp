#include "MenuPanelPresenter.h"

#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::menu
{
    namespace
    {
        constexpr const char* kBackgroundPanelName = "Background_Panel";
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleRightHandPanelName = "AlwaysVisibleRightHandPanel";

        bool IsAlwaysVisible(const std::string& name)
        {
            return name == kBackgroundPanelName || name == kPersistentPanelName ||
                   name == kAlwaysVisiblePanelName || name == kAlwaysVisibleRightHandPanelName;
        }

        void AttachToBoard(
            const std::shared_ptr<vrui::VRUIPanel>& panel,
            bool worldPinned,
            RE::NiNode* physicalAnchor,
            RE::NiNode* menuHand,
            RE::NiNode* pinnedAttachNode,
            const RE::NiPoint3& panelOffset)
        {
            if (physicalAnchor) {
                panel->attachToPhysicalNode(physicalAnchor);
            } else if (worldPinned && pinnedAttachNode) {
                panel->attachToNode(pinnedAttachNode);
            } else if (menuHand) {
                panel->attachToHandNode(menuHand, panelOffset);
            }
        }
    }

    void MenuPanelPresenter::PresentOpen(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        bool worldPinned,
        RE::NiNode* physicalAnchor,
        RE::NiNode* menuHand,
        RE::NiNode* leftHandNode,
        RE::NiNode* rightHandNode,
        RE::NiNode* pinnedAttachNode,
        const RE::NiPoint3& panelOffset)
    {
        for (const auto& panel : panels) {
            const auto& name = panel->getName();
            const bool alwaysVisible = IsAlwaysVisible(name);
            if (!panel->isActive() && !alwaysVisible) continue;
            if ((name == kAlwaysVisiblePanelName || name == kAlwaysVisibleRightHandPanelName) && !panel->isActive()) {
                continue;
            }

            if (alwaysVisible && name != kAlwaysVisiblePanelName && name != kAlwaysVisibleRightHandPanelName) {
                panel->setActive(true);
            }
            if (name == kAlwaysVisiblePanelName) {
                if (leftHandNode) {
                    panel->attachToHandNode(leftHandNode, panelOffset);
                }
            } else if (name == kAlwaysVisibleRightHandPanelName) {
                if (rightHandNode) {
                    panel->attachToHandNode(rightHandNode, panelOffset);
                }
            } else {
                AttachToBoard(panel, worldPinned, physicalAnchor, menuHand, pinnedAttachNode, panelOffset);
            }
            panel->show();
        }
    }

    void MenuPanelPresenter::PresentClosed(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        bool worldPinned,
        RE::NiNode* physicalAnchor,
        RE::NiNode* menuHand,
        RE::NiNode* pinnedAttachNode,
        RE::NiNode* leftHandNode,
        RE::NiNode* rightHandNode,
        const RE::NiPoint3& panelOffset)
    {
        for (const auto& panel : panels) {
            const auto& name = panel->getName();
            if (name == kAlwaysVisiblePanelName) {
                if (panel->isActive()) {
                    if (leftHandNode) {
                        panel->attachToHandNode(leftHandNode, panelOffset);
                    }
                    panel->show();
                } else {
                    panel->detachFromHandNode();
                }
                continue;
            }

            if (name == kAlwaysVisibleRightHandPanelName) {
                if (panel->isActive()) {
                    if (rightHandNode) {
                        panel->attachToHandNode(rightHandNode, panelOffset);
                    }
                    panel->show();
                } else {
                    panel->detachFromHandNode();
                }
                continue;
            }

            panel->detachFromHandNode();
        }
    }

    void MenuPanelPresenter::DetachAll(const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels)
    {
        for (const auto& panel : panels) {
            if (panel) panel->detachFromHandNode();
        }
    }
}
