#pragma once

#include "vrui/VRUISettings.h"

namespace dragonboard::ui::input
{
    struct ActivationInputs
    {
        bool grip = false;
        bool trigger = false;
        bool thumbstick = false;
        bool secondary = false;
    };

    struct ActivationHoldTimes
    {
        float grip = 0.0f;
        float trigger = 0.3f;
        float thumbstick = 0.15f;
    };

    class ActivationHoldTracker
    {
    public:
        bool Update(
            vrui::ActivationMode mode,
            const ActivationInputs& inputs,
            const ActivationHoldTimes& holdTimes,
            float deltaTime);

    private:
        float _gripTimer = 0.0f;
        float _triggerTimer = 0.0f;
        float _thumbstickTimer = 0.0f;
        bool _gripWasHeld = false;
        bool _triggerWasHeld = false;
        bool _thumbstickWasHeld = false;
    };
}
