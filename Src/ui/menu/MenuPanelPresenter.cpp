#include "MenuPanelPresenter.h"

#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::menu
{
    namespace
    {
        constexpr const char* kBackgroundPanelName = "Background_Panel";
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleHmdPanelName = "AlwaysVisibleHmdPanel";

        bool IsAlwaysVisible(const std::string& name)
        {
            return name == kBackgroundPanelName || name == kPersistentPanelName ||
                   name == kAlwaysVisiblePanelName || name == kAlwaysVisibleHmdPanelName;
        }

        void AttachToBoard(
            const std::shared_ptr<vrui::VRUIPanel>& panel,
            bool worldPinned,
            RE::NiNode* menuHand,
            RE::NiNode* pinnedAttachNode,
            const RE::NiPoint3& panelOffset)
        {
            if (worldPinned && pinnedAttachNode) {
                panel->attachToNode(pinnedAttachNode);
            } else if (menuHand) {
                panel->attachToHandNode(menuHand, panelOffset);
            }
        }
    }

    void MenuPanelPresenter::PresentOpen(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        bool worldPinned,
        RE::NiNode* menuHand,
        RE::NiNode* pinnedAttachNode,
        const RE::NiPoint3& panelOffset)
    {
        for (const auto& panel : panels) {
            const auto& name = panel->getName();
            const bool alwaysVisible = IsAlwaysVisible(name);
            if (!panel->isActive() && !alwaysVisible) continue;
            if ((name == kAlwaysVisiblePanelName || name == kAlwaysVisibleHmdPanelName) && !panel->isActive()) {
                continue;
            }

            if (alwaysVisible && name != kAlwaysVisiblePanelName && name != kAlwaysVisibleHmdPanelName) {
                panel->setActive(true);
            }
            AttachToBoard(panel, worldPinned, menuHand, pinnedAttachNode, panelOffset);
            panel->show();
        }
    }

    void MenuPanelPresenter::PresentClosed(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        bool worldPinned,
        RE::NiNode* menuHand,
        RE::NiNode* pinnedAttachNode,
        RE::NiNode* headNode,
        const RE::NiPoint3& panelOffset)
    {
        for (const auto& panel : panels) {
            const auto& name = panel->getName();
            if (name == kAlwaysVisiblePanelName) {
                if (panel->isActive()) {
                    AttachToBoard(panel, worldPinned, menuHand, pinnedAttachNode, panelOffset);
                    panel->show();
                } else {
                    panel->detachFromHandNode();
                }
                continue;
            }

            if (name == kAlwaysVisibleHmdPanelName) {
                if (panel->isActive()) {
                    if (headNode) {
                        panel->attachToNode(headNode);
                        panel->setLocalPosition({ 0.0f, 0.0f, 0.0f });
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
