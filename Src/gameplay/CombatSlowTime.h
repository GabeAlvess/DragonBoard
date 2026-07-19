#pragma once

namespace dragonboard::gameplay
{
    class CombatSlowTime
    {
    public:
        static CombatSlowTime& GetSingleton();
        void Open(bool playerInCombat, bool enabled, float multiplier);
        void Close();
        void Reconfigure(bool enabled, float multiplier);
        [[nodiscard]] bool IsActive() const noexcept { return _active; }

    private:
        CombatSlowTime() = default;
        [[nodiscard]] static float ClampMultiplier(float multiplier);
        [[nodiscard]] bool StillOwnsTimeMultiplier() const;
        void Apply(float multiplier);
        bool _active{ false };
        float _previousMultiplier{ 1.0f };
        float _appliedMultiplier{ 1.0f };
    };
}
