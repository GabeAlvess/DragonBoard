#include "InteractionInputController.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"
#include "vrui/VRUIWidget.h"

namespace dragonboard::ui::input
{
    void InteractionInputController::ProcessActivation(vrui::VRMenuManager& manager, float deltaTime)
    {
        auto& settings = vrui::VRUISettings::get();

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

        const auto secondaryEvents = manager._secondaryButtonTracker.Update(
            manager._inputButtons.Secondary(), deltaTime, 0.5f, manager._menuSession.IsOpen());
        if (secondaryEvents.longPress) {
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                hovered->onSecondaryLongPress();
                if (settings.hapticOnPress) {
                    manager.triggerHaptic(
                        false, settings.hapticIntensity * 1.5f, settings.hapticDuration * 2.0f);
                }
            }
        }
        if (secondaryEvents.released) {
            if (secondaryEvents.shortPress && manager._menuSession.IsOpen()) {
                if (auto hovered = manager._interactionFocus.GetHovered()) {
                    hovered->onSecondaryPress();
                    if (settings.hapticOnPress) {
                        manager.triggerHaptic(
                            false, settings.hapticIntensity, settings.hapticDuration);
                    }
                }
            }
            if (auto hovered = manager._interactionFocus.GetHovered()) {
                hovered->onSecondaryRelease();
            }
        }
    }
}
