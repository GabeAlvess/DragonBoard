#include "CooldownTimer.h"

namespace dragonboard::core
{
    void CooldownTimer::Advance(float deltaTime)
    {
        _elapsed += deltaTime;
    }

    bool CooldownTimer::IsReady(float cooldown) const
    {
        return _elapsed >= cooldown;
    }

    bool CooldownTimer::TryConsume(float cooldown)
    {
        if (!IsReady(cooldown)) {
            return false;
        }

        Consume();
        return true;
    }

    void CooldownTimer::Consume()
    {
        _elapsed = 0.0f;
    }
}
