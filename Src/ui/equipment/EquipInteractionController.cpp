#include "EquipInteractionController.h"

#include "ui/refresh/PanelRefresher.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::ui::equipment
{
    bool EquipInteractionController::CanExecute(const vrui::VRMenuManager& manager)
    {
        return manager._equipCooldown.IsReady(vrui::VRUISettings::get().equipCooldown);
    }

    void EquipInteractionController::NotifyExecuted(vrui::VRMenuManager& manager)
    {
        manager._equipCooldown.Consume();
    }

    void EquipInteractionController::RequestRefresh(
        vrui::VRMenuManager& manager,
        float delay)
    {
        manager._equipRefreshScheduler.Request(delay);
    }

    void EquipInteractionController::Update(vrui::VRMenuManager& manager, float deltaTime)
    {
        manager._equipCooldown.Advance(deltaTime);
        if (manager._equipRefreshScheduler.Update(deltaTime)) {
            dragonboard::ui::refresh::PanelRefresher::UpdateEquippedStates(
                manager._panelRegistry.GetPanels());
        }
    }
}
