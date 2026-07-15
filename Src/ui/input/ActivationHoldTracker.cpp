#include "ActivationHoldTracker.h"

namespace dragonboard::ui::input
{
    bool ActivationHoldTracker::Update(
        vrui::ActivationMode mode,
        const ActivationInputs& inputs,
        const ActivationHoldTimes& holdTimes,
        float deltaTime)
    {
        bool activationInput = false;
        float* holdTimer = nullptr;
        float holdTime = 0.3f;
        bool* wasHeld = nullptr;

        switch (mode) {
        case vrui::ActivationMode::Grip:
            activationInput = inputs.grip;
            holdTimer = &_gripTimer;
            holdTime = holdTimes.grip;
            wasHeld = &_gripWasHeld;
            break;
        case vrui::ActivationMode::Trigger:
            activationInput = inputs.trigger;
            holdTimer = &_triggerTimer;
            holdTime = holdTimes.trigger;
            wasHeld = &_triggerWasHeld;
            break;
        case vrui::ActivationMode::Thumbstick:
            activationInput = inputs.thumbstick;
            holdTimer = &_thumbstickTimer;
            holdTime = holdTimes.thumbstick;
            wasHeld = &_thumbstickWasHeld;
            break;
        case vrui::ActivationMode::GripPlusThumbstick:
            activationInput = inputs.grip && inputs.thumbstick;
            holdTimer = &_gripTimer;
            holdTime = holdTimes.grip;
            wasHeld = &_gripWasHeld;
            break;
        case vrui::ActivationMode::GripPlusY:
        case vrui::ActivationMode::GripPlusB:
            activationInput = inputs.grip && inputs.secondary;
            holdTimer = &_gripTimer;
            holdTime = holdTimes.grip;
            wasHeld = &_gripWasHeld;
            break;
        case vrui::ActivationMode::Hotkey8:
            break;
        }

        if (activationInput && holdTimer && wasHeld) {
            *holdTimer += deltaTime;
            if (*holdTimer >= holdTime && !*wasHeld) {
                *wasHeld = true;
                return true;
            }
        } else if (holdTimer && wasHeld) {
            *holdTimer = 0.0f;
            *wasHeld = false;
        }

        return false;
    }
}
