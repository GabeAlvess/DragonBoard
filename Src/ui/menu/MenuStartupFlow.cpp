#include "MenuStartupFlow.h"

#include "ui/widgets/WidgetTree.h"
#include "vrui/VRUIDynamicContainer.h"
#include "vrui/VRUIPanel.h"

namespace dragonboard::ui::menu
{
    namespace
    {
        std::shared_ptr<vrui::VRUIPanel> FindPanel(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const char* name)
        {
            for (const auto& panel : panels) {
                if (panel && panel->getName() == name) return panel;
            }
            return nullptr;
        }
    }

    MenuStartupResult MenuStartupFlow::Prepare(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        const std::string& configuredAction)
    {
        MenuStartupResult result;
        result.action = configuredAction.empty() || configuredAction == "None" ?
            "MainPanel" : configuredAction;
        std::shared_ptr<vrui::VRUIPanel> startupPanel;
        for (const auto& panel : panels) {
            if (panel->getName() == result.action) {
                result.isVrPanel = true;
                startupPanel = panel;
                break;
            }
        }

        // Opening DragonBoard must not synchronously rebuild every hidden
        // inventory/magic/favorites container. Only the configured startup
        // panel is primed, and its refresh runs from the normal frame update.
        if (startupPanel) {
            std::vector<vrui::VRUIDynamicContainer*> containers;
            dragonboard::ui::widgets::WidgetTree::CollectDynamicContainers(
                startupPanel.get(), containers);
            for (auto* container : containers) {
                container->resetPage();
                container->scheduleRefresh(0.0f);
            }
        }
        return result;
    }

    void MenuStartupFlow::TriggerEntranceAnimations(
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels)
    {
        float accumulatedDelay = 0.0f;
        if (auto panel = FindPanel(panels, "Persistent_Panel")) {
            panel->triggerEntranceAnimation(accumulatedDelay);
        }
        if (auto panel = FindPanel(panels, "AlwaysVisiblePanel"); panel && panel->isActive()) {
            panel->triggerEntranceAnimation(accumulatedDelay);
        }
        if (auto panel = FindPanel(panels, "AlwaysVisibleRightHandPanel"); panel && panel->isActive()) {
            panel->triggerEntranceAnimation(accumulatedDelay);
        }
        if (auto panel = FindPanel(panels, "MainPanel")) {
            panel->triggerEntranceAnimation(accumulatedDelay);
        }
        if (auto panel = FindPanel(panels, "Background_Panel")) {
            panel->triggerEntranceAnimation(accumulatedDelay);
        }
    }
}
