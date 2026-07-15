#pragma once

namespace dragonboard::ui::settings
{
    class SettingsSaveScheduler
    {
    public:
        void Reset();
        void Request(float delaySeconds);
        [[nodiscard]] bool Update(float deltaTime);

        [[nodiscard]] bool IsPending() const { return _pending; }

    private:
        bool _pending{ false };
        float _remainingDelay{ -1.0f };
    };
}
