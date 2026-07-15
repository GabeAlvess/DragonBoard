#include "RefreshPipelineController.h"

#include "PanelRefresher.h"
#include "ui/panels/PanelTransformUpdater.h"
#include "ui/widgets/FixedWidgetPresenter.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::ui::refresh
{
    void RefreshPipelineController::RefreshFixedWidgets(vrui::VRMenuManager& manager)
    {
        dragonboard::ui::widgets::FixedWidgetPresenter::Refresh(manager);
    }

    void RefreshPipelineController::UpdatePanelTransforms(vrui::VRMenuManager& manager)
    {
        if (!manager._menuSession.IsOpen()) return;

        auto* handNode = manager.getMenuHandNode();
        auto* pinnedAttachNode = manager.resolvePinnedAttachNode(manager.getPlayerSkeletonRoot());
        auto* headNode = manager.getHeadNode();
        dragonboard::ui::panels::PanelTransformUpdater::Update(
            manager._panelRegistry.GetPanels(),
            handNode,
            pinnedAttachNode,
            headNode,
            manager._boardPinState.IsPinned(),
            manager.getPanelOffset());
    }

    void RefreshPipelineController::ApplyFullRefresh(vrui::VRMenuManager& manager)
    {
        auto& settings = vrui::VRUISettings::get();
        PanelRefresher::RefreshLayouts(
            manager._panelRegistry.GetPanels(),
            settings.buttonSpacingX,
            settings.buttonSpacingY);

        RefreshFixedWidgets(manager);
        UpdatePanelTransforms(manager);
    }

    void RefreshPipelineController::ProcessPending(vrui::VRMenuManager& manager)
    {
        if (manager._refreshCoordinator.TakeAll()) {
            ApplyFullRefresh(manager);
            return;
        }

        if (manager._refreshCoordinator.TakeDynamic()) {
            const auto& settings = vrui::VRUISettings::get();
            PanelRefresher::RefreshDynamic(
                manager._panelRegistry.FindActive(),
                settings.buttonSpacingX,
                settings.buttonSpacingY);
        }

        if (manager._refreshCoordinator.TakeFixedWidgets()) {
            RefreshFixedWidgets(manager);
        }

        if (manager._refreshCoordinator.TakeTransforms()) {
            UpdatePanelTransforms(manager);
        }
    }
}
