#include "ui/rml/RmlEntranceAnimation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

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

    RmlEntranceStyle ParseRmlEntranceStyle(std::string_view value)
    {
        std::string compact;
        compact.reserve(value.size());
        for (const unsigned char character : value) {
            if (std::isalnum(character)) {
                compact.push_back(static_cast<char>(std::tolower(character)));
            }
        }

        if (compact == "reverseradial") return RmlEntranceStyle::kReverseRadial;
        if (compact == "fade" || compact == "instant") return RmlEntranceStyle::kFade;
        if (compact == "lefttoright") return RmlEntranceStyle::kLeftToRight;
        if (compact == "righttoleft") return RmlEntranceStyle::kRightToLeft;
        return RmlEntranceStyle::kRadial;
    }

    bool RmlEntranceAnimation::Configure(
        bool enabled,
        float durationSeconds,
        float feather,
        RmlEntranceStyle style)
    {
        const float previousProgress = GetProgress();
        const float previousFeather = _feather;
        const auto previousStyle = _style;

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
        _style = style;

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
               (_active && std::abs(previousFeather - _feather) > kComparisonEpsilon) ||
               previousStyle != _style;
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
