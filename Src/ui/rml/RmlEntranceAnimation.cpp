#include "ui/rml/RmlEntranceAnimation.h"

#include <algorithm>
#include <cmath>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr float kMinimumDurationSeconds = 0.05f;
        constexpr float kMaximumDurationSeconds = 2.0f;
        constexpr float kMaximumFeather = 0.50f;
        constexpr float kDefaultDurationSeconds = 0.25f;
        constexpr float kDefaultFeather = 0.10f;
        constexpr float kComparisonEpsilon = 0.00001f;
    }

    bool RmlEntranceAnimation::Configure(
        bool enabled,
        float durationSeconds,
        float feather)
    {
        const float previousProgress = GetProgress();
        const float previousFeather = _feather;

        if (!std::isfinite(durationSeconds)) {
            durationSeconds = kDefaultDurationSeconds;
        }
        if (!std::isfinite(feather)) {
            feather = kDefaultFeather;
        }

        _enabled = enabled;
        _durationSeconds = std::clamp(
            durationSeconds,
            kMinimumDurationSeconds,
            kMaximumDurationSeconds);
        _feather = std::clamp(feather, 0.0f, kMaximumFeather);

        if (!_enabled) {
            Stop();
        } else if (_active) {
            _linearProgress = std::clamp(
                _elapsedSeconds / _durationSeconds,
                0.0f,
                1.0f);
            if (_linearProgress >= 1.0f) {
                _active = false;
            }
        }

        return std::abs(previousProgress - GetProgress()) > kComparisonEpsilon ||
               (_active && std::abs(previousFeather - _feather) > kComparisonEpsilon);
    }

    void RmlEntranceAnimation::Start()
    {
        if (!_enabled) {
            Stop();
            return;
        }

        _elapsedSeconds = 0.0f;
        _linearProgress = 0.0f;
        _active = true;
    }

    void RmlEntranceAnimation::Stop()
    {
        _elapsedSeconds = _durationSeconds;
        _linearProgress = 1.0f;
        _active = false;
    }

    bool RmlEntranceAnimation::Advance(float deltaSeconds)
    {
        if (!_active || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return false;

        const float previousProgress = _linearProgress;
        _elapsedSeconds += deltaSeconds;
        _linearProgress = std::clamp(
            _elapsedSeconds / _durationSeconds,
            0.0f,
            1.0f);
        if (_linearProgress >= 1.0f) {
            _active = false;
        }
        return std::abs(previousProgress - _linearProgress) > kComparisonEpsilon;
    }

    float RmlEntranceAnimation::GetProgress() const
    {
        const float remaining = 1.0f - _linearProgress;
        return 1.0f - remaining * remaining * remaining;
    }
}
