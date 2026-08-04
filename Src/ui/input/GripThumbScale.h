#pragma once

#include <algorithm>
#include <cmath>

namespace dragonboard::ui::input
{
    struct GripThumbScaleResult
    {
        float scale = 1.0f;
        bool active = false;
        bool changed = false;
    };

    [[nodiscard]] inline GripThumbScaleResult ApplyGripThumbScale(
        float currentScale,
        float thumbstickY,
        float deltaTime,
        float minimumScale,
        float maximumScale)
    {
        constexpr float kThumbDeadzone = 0.20f;
        constexpr float kThumbScaleSpeed = 1.25f;

        GripThumbScaleResult result;
        result.scale = std::clamp(
            currentScale,
            std::max(minimumScale, 1.0e-4f),
            std::max(maximumScale, minimumScale));

        const float magnitude = std::abs(thumbstickY);
        if (magnitude <= kThumbDeadzone) {
            return result;
        }

        const float normalizedMagnitude =
            (magnitude - kThumbDeadzone) / (1.0f - kThumbDeadzone);
        const float signedInput = std::copysign(
            normalizedMagnitude,
            thumbstickY);
        const float newScale = std::clamp(
            result.scale * std::exp(
                signedInput * kThumbScaleSpeed * std::max(deltaTime, 0.0f)),
            std::max(minimumScale, 1.0e-4f),
            std::max(maximumScale, minimumScale));

        result.active = true;
        result.changed = std::abs(newScale - result.scale) > 1.0e-6f;
        result.scale = newScale;
        return result;
    }
}
