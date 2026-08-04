#include "PanelNavigator.h"

#include "ui/widgets/WidgetTree.h"
#include "vrui/VRUIDynamicContainer.h"
#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::panels
{
    namespace
    {
        constexpr const char* kBackgroundPanelName = "Background_Panel";
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleRightHandPanelName = "AlwaysVisibleRightHandPanel";

        bool IsPersistentPanel(const std::string& name)
        {
            return name == kBackgroundPanelName || name == kPersistentPanelName ||
                   name == kAlwaysVisiblePanelName || name == kAlwaysVisibleRightHandPanelName;
        }
    }

    PanelSwitchResult PanelNavigator::SwitchTo(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        const std::string& panelName,
        bool menuOpen,
        bool worldPinned,
        RE::NiNode* physicalAnchor,
        RE::NiNode* handNode,
        RE::NiNode* pinnedAttachNode,
        const RE::NiPoint3& panelOffset)
    {
        std::shared_ptr<vrui::VRUIPanel> targetPanel;
        for (const auto& panel : panels) {
            if (panel->getName() == panelName) {
                targetPanel = panel;
            }
        }
        if (!targetPanel) {
            return {};
        }

        for (const auto& panel : panels) {
            if (panel->isActive() && !IsPersistentPanel(panel->getName())) {
                panel->setActive(false);
                panel->hide();
                panel->setVisible(false);
                panel->detachFromParent();
            }
        }

        targetPanel->setActive(true);
        PanelSwitchResult result{ .found = true };
        if (!menuOpen) {
            return result;
        }

        if (physicalAnchor) {
            targetPanel->attachToPhysicalNode(physicalAnchor);
        } else if (worldPinned && pinnedAttachNode) {
            targetPanel->attachToNode(pinnedAttachNode);
        } else if (handNode) {
            targetPanel->attachToHandNode(handNode, panelOffset);
        }

        std::vector<vrui::VRUIDynamicContainer*> dynamicContainers;
        dragonboard::ui::widgets::WidgetTree::CollectDynamicContainers(
            targetPanel.get(), dynamicContainers);
        for (auto* container : dynamicContainers) {
            if (container) {
                // Defer content discovery/build until the panel is actually
                // selected instead of blocking the DragonBoard open path.
                container->scheduleRefresh(0.0f);
            }
        }

        targetPanel->recalculateLayout();
        targetPanel->show();
        result.pageableContainer = dragonboard::ui::widgets::WidgetTree::FindFirstGrid(targetPanel.get());
        return result;
    }
}
