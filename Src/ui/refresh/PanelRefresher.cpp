#include "PanelRefresher.h"

#include "ui/widgets/WidgetTree.h"
#include "vrui/VRUIDynamicContainer.h"
#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::refresh
{
    void PanelRefresher::UpdateEquippedStates(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels)
    {
        std::vector<vrui::VRUIDynamicContainer*> containers;
        for (const auto& panel : panels) {
            dragonboard::ui::widgets::WidgetTree::CollectDynamicContainers(panel.get(), containers);
        }
        for (auto* container : containers) {
            container->updateEquippedStates();
        }
    }

    void PanelRefresher::RefreshDynamic(
        const std::shared_ptr<vrui::VRUIPanel>& panel,
        float spacingX,
        float spacingY)
    {
        if (!panel) return;

        std::vector<vrui::VRUIDynamicContainer*> containers;
        dragonboard::ui::widgets::WidgetTree::CollectDynamicContainers(panel.get(), containers);
        for (auto* container : containers) {
            if (container) {
                container->invalidateRefreshCache();
                container->refresh();
            }
        }
        dragonboard::ui::widgets::WidgetTree::UpdateContainerSpacing(panel.get(), spacingX, spacingY, 0.0f);
        panel->recalculateLayout();
    }

    void PanelRefresher::RefreshLayouts(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        float spacingX,
        float spacingY)
    {
        for (const auto& panel : panels) {
            if (!panel) continue;
            dragonboard::ui::widgets::WidgetTree::UpdateContainerSpacing(panel.get(), spacingX, spacingY, 0.0f);
            panel->recalculateLayout();
        }
    }
}
