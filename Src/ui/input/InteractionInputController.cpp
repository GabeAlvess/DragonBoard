#include "InteractionInputController.h"

#include "ui/rml/RmlPanelHost.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUISettings.h"
#include "vrui/VRUIWidget.h"

#include <chrono>

namespace dragonboard::ui::input
{
    void InteractionInputController::ProcessActivation(vrui::VRMenuManager& manager, float deltaTime)
    {
        auto& settings = vrui::VRUISettings::get();

        if (manager.isPhysicalBoardActive()) {
            manager._activationHoldTracker.Update(
                settings.activationMode, {}, {}, deltaTime);
            return;
        }

        if (manager.isPositionAdjustmentActive()) {
            // Feed a released state so a partially accumulated activation hold
            // cannot close the board while grip is reserved for adjustment.
            manager._activationHoldTracker.Update(
                settings.activationMode, {}, {}, deltaTime);
            return;
        }

        const ActivationInputs inputs{
            .grip = manager._inputButtons.Grip(),
            .trigger = manager._inputButtons.ActivationTrigger(),
            .thumbstick = manager._inputButtons.Thumbstick(),
            .secondary = manager._inputButtons.Secondary()
        };
        const ActivationHoldTimes holdTimes{
            .grip = settings.activationHoldTimeGrip,
            .trigger = settings.activationHoldTimeTrigger,
            .thumbstick = settings.activationHoldTimeThumbstick
        };

        if (manager._activationHoldTracker.Update(
                settings.activationMode, inputs, holdTimes, deltaTime)) {
            manager.toggleMenu();
        }
    }

    void InteractionInputController::ProcessButtons(vrui::VRMenuManager& manager, float deltaTime)
    {
        auto& settings = vrui::VRUISettings::get();

        const auto dominantEvents = manager._dominantTriggerTracker.Update(
            manager._inputButtons.Trigger(), deltaTime, 1.0f);
        if (dominantEvents.longPress) {
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                hovered->onTriggerLongPress(vrui::EquipHand::kRight);
                if (settings.hapticOnPress) {
                    manager.triggerHaptic(
                        true, settings.hapticIntensity * 1.5f, settings.hapticDuration * 2.0f);
                }
            }
        }
        if (dominantEvents.released) {
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                if (dominantEvents.shortPress) {
                    hovered->onTriggerPress(vrui::EquipHand::kRight);
                    if (settings.hapticOnPress) {
                        manager.triggerHaptic(
                            true, settings.hapticIntensity, settings.hapticDuration);
                    }
                }
                hovered->onTriggerRelease(vrui::EquipHand::kRight);
            }
        }

        const auto offhandEvents = manager._offhandTriggerTracker.Update(
            manager._inputButtons.OffhandTrigger(), deltaTime, 1.0f);
        if (offhandEvents.longPress) {
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                hovered->onTriggerLongPress(vrui::EquipHand::kLeft);
                if (settings.hapticOnPress) {
                    manager.triggerHaptic(
                        false, settings.hapticIntensity * 1.5f, settings.hapticDuration * 2.0f);
                }
            }
        }
        if (offhandEvents.released) {
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                if (offhandEvents.shortPress) {
                    hovered->onTriggerPress(vrui::EquipHand::kLeft);
                    if (settings.hapticOnPress) {
                        manager.triggerHaptic(
                            false, settings.hapticIntensity, settings.hapticDuration);
                    }
                }
                hovered->onTriggerRelease(vrui::EquipHand::kLeft);
            }
        }

        const bool secondaryPressed = manager._inputButtons.Secondary() &&
            !manager._secondaryButtonTracker.IsPressed();
        const bool dominantSecondaryPressed = manager._inputButtons.DominantSecondary() &&
            !manager._dominantSecondaryButtonTracker.IsPressed();
        const auto secondaryEvents = manager._secondaryButtonTracker.Update(
            manager._inputButtons.Secondary(), deltaTime, 0.5f, manager._menuSession.IsOpen());
        const auto dominantSecondaryEvents = manager._dominantSecondaryButtonTracker.Update(
            manager._inputButtons.DominantSecondary(),
            deltaTime,
            0.5f,
            manager._menuSession.IsOpen());
        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        const bool modsRmlActive = rmlHost.IsModsOpen();
        const bool secondaryOnRightHand = manager.isMenuHandLeft();
        const auto& panelSecondaryEvents = modsRmlActive ?
            dominantSecondaryEvents : secondaryEvents;
        bool removedGrabbedTarget = false;
        if (!modsRmlActive &&
            (secondaryPressed || dominantSecondaryPressed)) {
            if (settings.editModeEnabled && !settings.lockPins) {
                const auto grabbedWidget = manager.getGrabbedWidget();
                const auto grabbedButton = grabbedWidget ?
                    std::dynamic_pointer_cast<vrui::VRUIButton>(grabbedWidget) : nullptr;
                if (grabbedButton && grabbedButton->isGrabbed() &&
                    grabbedButton->isDashboardPinned()) {
                    const auto dispatchStartedAt = std::chrono::steady_clock::now();
                    logger::info(
                        "DragonBoardVR: pinned removal input accepted for '{}'.",
                        grabbedButton->getButtonId());
                    grabbedButton->onSecondaryPress();
                    const auto dispatchFinishedAt = std::chrono::steady_clock::now();
                    const auto dispatchMs = std::chrono::duration<double, std::milli>(
                        dispatchFinishedAt - dispatchStartedAt).count();
                    logger::info(
                        "DragonBoardVR: pinned removal callback for '{}' returned in {:.3f} ms.",
                        grabbedButton->getButtonId(),
                        dispatchMs);
                    removedGrabbedTarget = true;
                }
            }
            if (!removedGrabbedTarget) {
                removedGrabbedTarget = rmlHost.RequestGrabbedSurfaceRemoval();
            }
            if (removedGrabbedTarget && settings.hapticOnPress) {
                const bool rightHand = dominantSecondaryPressed ?
                    !manager.isDominantHandLeft() : secondaryOnRightHand;
                manager.triggerHaptic(
                    rightHand,
                    settings.hapticIntensity * 1.5f,
                    settings.hapticDuration * 2.0f);
            }
            if (removedGrabbedTarget) {
                manager._secondaryButtonTracker.SuppressUntilRelease();
                manager._dominantSecondaryButtonTracker.SuppressUntilRelease();
            }
        }
        if (!removedGrabbedTarget && panelSecondaryEvents.longPress) {
            if (modsRmlActive) {
                if (rmlHost.RequestHoveredModRemoval() && settings.hapticOnPress) {
                    manager.triggerHaptic(
                        secondaryOnRightHand,
                        settings.hapticIntensity * 1.5f,
                        settings.hapticDuration * 2.0f);
                }
            } else if (auto hovered = manager._interactionFocus.GetHovered()) {
                const auto hoveredButton =
                    std::dynamic_pointer_cast<vrui::VRUIButton>(hovered);
                if (!hoveredButton || !hoveredButton->isDashboardPinned()) {
                    hovered->onSecondaryLongPress();
                    if (settings.hapticOnPress) {
                        manager.triggerHaptic(
                            false,
                            settings.hapticIntensity * 1.5f,
                            settings.hapticDuration * 2.0f);
                    }
                }
            }
        }
        if (panelSecondaryEvents.released) {
            if (panelSecondaryEvents.shortPress && manager._menuSession.IsOpen()) {
                if (modsRmlActive) {
                    if (rmlHost.RequestHoveredModOptions() && settings.hapticOnPress) {
                        manager.triggerHaptic(
                            secondaryOnRightHand,
                            settings.hapticIntensity,
                            settings.hapticDuration);
                    }
                } else if (auto hovered = manager._interactionFocus.GetHovered()) {
                    hovered->onSecondaryPress();
                    if (settings.hapticOnPress) {
                        manager.triggerHaptic(
                            false, settings.hapticIntensity, settings.hapticDuration);
                    }
                }
            }
            if (!modsRmlActive) {
                if (auto hovered = manager._interactionFocus.GetHovered()) {
                    hovered->onSecondaryRelease();
                }
            }
        }
    }
}
