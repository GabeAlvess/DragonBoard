#include "PanelManagementController.h"

#include "PanelNavigator.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIContainer.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::ui::panels
{
    namespace
    {
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleRightHandPanelName = "AlwaysVisibleRightHandPanel";
    }

    void PanelManagementController::Register(
        vrui::VRMenuManager& manager,
        std::shared_ptr<vrui::VRUIPanel> panel)
    {
        manager._panelRegistry.Register(std::move(panel));
        logger::trace(
            "DragonBoardVR: Panel registered (total: {})", manager._panelRegistry.Size());
    }

    void PanelManagementController::Unregister(
        vrui::VRMenuManager& manager,
        const std::shared_ptr<vrui::VRUIPanel>& panel)
    {
        if (!manager._panelRegistry.Contains(panel)) return;

        panel->hide();
        panel->detachFromParent();
        manager._panelRegistry.Unregister(panel);
        logger::trace(
            "DragonBoardVR: Panel unregistered (total: {})", manager._panelRegistry.Size());
    }

    RE::NiPoint3 PanelManagementController::GetPanelOffset()
    {
        auto& settings = vrui::VRUISettings::get();
        return { settings.menuOffsetX, settings.menuOffsetY, settings.menuOffsetZ };
    }

    RE::NiNode* PanelManagementController::ResolvePinnedAttachNode(
        const vrui::VRMenuManager& manager,
        RE::NiNode* skeletonRoot)
    {
        if (auto* headNode = manager.getHeadNode()) {
            return headNode;
        }

        if (!skeletonRoot) return nullptr;

        if (auto* parentNode = skeletonRoot->parent ? skeletonRoot->parent->AsNode() : nullptr) {
            return parentNode;
        }

        return skeletonRoot;
    }

    void PanelManagementController::SwitchTo(
        vrui::VRMenuManager& manager,
        const std::string& panelName)
    {
        const auto result = PanelNavigator::SwitchTo(
            manager._panelRegistry.GetPanels(),
            panelName,
            manager._menuSession.IsOpen(),
            manager._boardPinState.IsPinned(),
            manager.getPhysicalBoardAnchorNode(),
            manager.getMenuHandNode(),
            ResolvePinnedAttachNode(manager, manager.getPlayerSkeletonRoot()),
            GetPanelOffset());

        if (!result.found) {
            logger::warn(
                "DragonBoardVR: switchToPanel failed. Target '{}' not found or same as current.",
                panelName);
            return;
        }

        if (result.pageableContainer) {
            manager.setActivePageableContainer(result.pageableContainer);
            logger::trace(
                "DragonBoardVR: Active pageable container set to '{}'",
                result.pageableContainer->getName());
        }

        logger::trace(
            "DragonBoardVR: Switched active panel to '{}' (MenuOpen: {})",
            panelName,
            manager._menuSession.IsOpen());
    }

    std::shared_ptr<vrui::VRUIPanel> PanelManagementController::FindByName(
        vrui::VRMenuManager& manager,
        const std::string& name)
    {
        return manager._panelRegistry.FindByName(name);
    }

    void PanelManagementController::SetWorldPinned(vrui::VRMenuManager& manager, bool pinned)
    {
        if (!manager._boardPinState.SetPinned(pinned)) return;

        if (pinned) {
            manager._boardPinState.RememberHandScale(vrui::VRUISettings::get().menuScale);
            std::shared_ptr<vrui::VRUIPanel> sourcePanel;
            for (auto& panel : manager._panelRegistry.GetPanels()) {
                if (panel && panel->isActive() && panel->isShown() &&
                    panel->getName() != "Background_Panel" &&
                    panel->getName() != kPersistentPanelName &&
                    panel->getName() != kAlwaysVisiblePanelName &&
                    panel->getName() != kAlwaysVisibleRightHandPanelName) {
                    sourcePanel = panel;
                    break;
                }
            }
            if (!sourcePanel) {
                sourcePanel = FindByName(manager, "MainPanel");
            }

            if (sourcePanel && sourcePanel->getNode()) {
                RE::NiUpdateData updateData;
                updateData.flags = RE::NiUpdateData::Flag::kDirty;
                sourcePanel->getNode()->Update(updateData);
                manager._boardPinState.Capture(
                    sourcePanel->getWorldPosition(),
                    sourcePanel->getWorldRotation(),
                    sourcePanel->getWorldScale(),
                    vrui::VRUISettings::get().menuScale);

                if (vrui::VRUISettings::get().verboseLogging) {
                    auto* pinnedAttachNode = ResolvePinnedAttachNode(
                        manager, manager.getPlayerSkeletonRoot());
                    const char* sourceParentName =
                        (sourcePanel->getNode()->parent &&
                         !sourcePanel->getNode()->parent->name.empty()) ?
                            sourcePanel->getNode()->parent->name.c_str() : "<unnamed>";
                    const char* attachName =
                        (pinnedAttachNode && !pinnedAttachNode->name.empty()) ?
                            pinnedAttachNode->name.c_str() : "<unnamed>";
                    logger::trace(
                        "DragonBoardVR: Board pinned sourcePanel='{}' sourceParent='{}' attachNode='{}' savedPos=({:.2f},{:.2f},{:.2f}) scale={:.3f}",
                        sourcePanel->getName(),
                        sourceParentName,
                        attachName,
                        manager._boardPinState.GetPosition().x,
                        manager._boardPinState.GetPosition().y,
                        manager._boardPinState.GetPosition().z,
                        manager._boardPinState.GetWorldScale());
                }
            } else {
                manager._boardPinState.SetFallbackScale(vrui::VRUISettings::get().menuScale);
            }
        } else {
            vrui::VRUISettings::get().menuScale = manager._boardPinState.GetHandMenuScale();
        }

        if (manager._menuSession.IsOpen()) {
            manager.refreshActivePanels();
        }
    }
}
