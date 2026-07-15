#include "ui/refresh/EquipRefreshScheduler.h"

namespace dragonboard::ui::refresh
{
    void EquipRefreshScheduler::Request(float delaySeconds)
    {
        if (_remainingDelay < 0.0f || delaySeconds < _remainingDelay) {
            _remainingDelay = delaySeconds;
        }
    }

    bool EquipRefreshScheduler::Update(float deltaTime)
    {
        if (_remainingDelay < 0.0f) {
            return false;
        }

        _remainingDelay -= deltaTime;
        if (_remainingDelay >= 0.0f) {
            return false;
        }

        _remainingDelay = -1.0f;
        return true;
    }
}
