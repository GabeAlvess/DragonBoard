#pragma once

namespace dragonboard::ui::refresh
{
    class EquipRefreshScheduler
    {
    public:
        void Request(float delaySeconds);
        [[nodiscard]] bool Update(float deltaTime);

        [[nodiscard]] bool IsPending() const { return _remainingDelay >= 0.0f; }

    private:
        float _remainingDelay{ -1.0f };
    };
}
