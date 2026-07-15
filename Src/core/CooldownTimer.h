#pragma once

namespace dragonboard::core
{
    class CooldownTimer
    {
    public:
        void Advance(float deltaTime);
        bool IsReady(float cooldown) const;
        bool TryConsume(float cooldown);
        void Consume();

    private:
        float _elapsed = 0.0f;
    };
}
