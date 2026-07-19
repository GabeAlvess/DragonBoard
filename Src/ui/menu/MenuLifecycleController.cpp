#include "MenuLifecycleController.h"

#include "MenuPanelPresenter.h"
#include "MenuStartupFlow.h"
#include "runtime/vr/GameMenuActions.h"
#include "ui/pointer/PointerVisualController.h"
#include "ui/rml/RmlPanelHost.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

#include <chrono>

namespace dragonboard::ui::menu
{
    void MenuLifecycleController::ApplySafeClose(vrui::VRMenuManager& manager)
    {
        if (!manager._menuSession.Close()) return;

        logger::trace("DragonBoardVR: Menu closed (safe close for command execution)");
        MenuPanelPresenter::DetachAll(manager._panelRegistry.GetPanels());
        dragonboard::ui::pointer::PointerVisualController::Hide(manager);
    }

    void MenuLifecycleController::ApplyToggle(vrui::VRMenuManager& manager)
    {
        const auto toggleStarted = std::chrono::steady_clock::now();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("DragonBoardVR: Cannot toggle menu. Player is missing.");
            return;
        }

        if (!manager._menuSession.IsOpen() &&
            (!player->Is3DLoaded() || !player->Get3D(!manager._isVRIKInstalled))) {
            logger::warn("DragonBoardVR: Cannot open menu. Player 3D is not fully loaded.");
            return;
        }

        const bool menuOpen = manager._menuSession.Toggle();
        logger::trace("DragonBoardVR: Menu toggled {}", menuOpen ? "OPEN" : "CLOSED");

        auto* menuHand = manager.getMenuHandNode();
        auto* skeletonRoot = manager.getPlayerSkeletonRoot();
        auto* pinnedAttachNode = manager.resolvePinnedAttachNode(skeletonRoot);
        auto& settings = vrui::VRUISettings::get();

        if (menuOpen) {
            for (int i = 0; i < vrui::VRUISettings::kMaxSlots; ++i) {
                settings.slotFloatingCache[i] = settings.slotFloating[i];
            }

            const auto startup = MenuStartupFlow::Prepare(
                manager._panelRegistry.GetPanels(), settings.defaultPanelAction);

            if (startup.isVrPanel) {
                manager.switchToPanel(startup.action);
            } else {
                manager.switchToPanel("MainPanel");

                if (startup.action == "Journal" || startup.action == "JournalMenu") {
                    dragonboard::ui::rml::RmlPanelHost::GetSingleton().OpenJournal();
                } else if (startup.action == "QuickSave" || startup.action == "Save") {
                    manager.toggleMenu();
                    dragonboard::runtime::vr::QueueQuickSave();
                } else {
                    manager.toggleMenu();
                    dragonboard::runtime::vr::ShowGameMenu(startup.action);
                }
            }

            MenuStartupFlow::TriggerEntranceAnimations(manager._panelRegistry.GetPanels());
        }

        const auto panelOffset = manager.getPanelOffset();
        if (menuOpen) {
            MenuPanelPresenter::PresentOpen(
                manager._panelRegistry.GetPanels(),
                manager._boardPinState.IsPinned(),
                menuHand,
                pinnedAttachNode,
                panelOffset);
        } else {
            // Release transient input/RmlUi references, but keep the existing
            // scene graph. Rebuilding the whole menu here detached and recreated
            // hand/HMD-pinned widgets, producing a visible blink on every close.
            auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
            rmlHost.Close();
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                hovered->onRayExit();
                if (manager._dominantTriggerTracker.IsPressed()) {
                    hovered->onTriggerRelease();
                }
            }
            manager._interactionFocus.ClearHover();
            manager._interactionFocus.ClearGrabbed();
            manager._dominantTriggerTracker.Reset();

            // PresentClosed detaches ordinary board panels while deliberately
            // preserving AlwaysVisiblePanel and AlwaysVisibleHmdPanel in place.
            MenuPanelPresenter::PresentClosed(
                manager._panelRegistry.GetPanels(),
                manager._boardPinState.IsPinned(),
                manager.getMenuHandNode(),
                manager.resolvePinnedAttachNode(manager.getPlayerSkeletonRoot()),
                manager.getHeadNode(),
                manager.getPanelOffset());
            dragonboard::ui::pointer::PointerVisualController::Hide(manager);
        }

        manager.triggerHaptic(false, 0.5f, 0.2f);
        const auto toggleMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - toggleStarted).count();
        logger::info(
            "DragonBoardVR: menu toggle {} completed in {} ms.",
            menuOpen ? "OPEN" : "CLOSED",
            toggleMs);
    }
}
