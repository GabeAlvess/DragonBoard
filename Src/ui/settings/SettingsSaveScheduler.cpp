#include "ui/settings/SettingsSaveScheduler.h"

namespace dragonboard::ui::settings
{
    void SettingsSaveScheduler::Reset()
    {
        _pending = false;
        _remainingDelay = -1.0f;
    }

    void SettingsSaveScheduler::Request(float delaySeconds)
    {
        const float clampedDelay = delaySeconds > 0.0f ? delaySeconds : 0.0f;
        _pending = true;
        if (_remainingDelay < 0.0f || clampedDelay < _remainingDelay) {
            _remainingDelay = clampedDelay;
        }
    }

    bool SettingsSaveScheduler::Update(float deltaTime)
    {
        if (!_pending || _remainingDelay < 0.0f) {
            return false;
        }

        _remainingDelay -= deltaTime;
        if (_remainingDelay > 0.0f) {
            return false;
        }

        Reset();
        return true;
    }
}
