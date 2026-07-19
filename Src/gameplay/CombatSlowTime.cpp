#include "gameplay/CombatSlowTime.h"

#include <RE/B/BSTimer.h>

#include <algorithm>
#include <cmath>

namespace dragonboard::gameplay
{
    namespace
    {
        constexpr float kMinimumMultiplier = 0.05f;
        constexpr float kOwnershipEpsilon = 0.005f;

        float ReadTimeMultiplier()
        {
            float multiplier = RE::BSTimer::QGlobalTimeMultiplierTarget();
            if (!std::isfinite(multiplier) || multiplier <= 0.0f) {
                multiplier = RE::BSTimer::QGlobalTimeMultiplier();
            }
            return std::isfinite(multiplier) && multiplier > 0.0f ? multiplier : 1.0f;
        }
    }

    CombatSlowTime& CombatSlowTime::GetSingleton()
    {
        static CombatSlowTime instance;
        return instance;
    }

    float CombatSlowTime::ClampMultiplier(float multiplier)
    {
        if (!std::isfinite(multiplier)) return 1.0f;
        return std::clamp(multiplier, kMinimumMultiplier, 1.0f);
    }

    bool CombatSlowTime::StillOwnsTimeMultiplier() const
    {
        return std::abs(ReadTimeMultiplier() - _appliedMultiplier) <= kOwnershipEpsilon;
    }

    void CombatSlowTime::Apply(float multiplier)
    {
        auto* timer = RE::BSTimer::GetSingleton();
        if (!timer) {
            logger::warn("DragonBoardVR: BSTimer unavailable; combat slow time was not applied.");
            return;
        }
        _appliedMultiplier = multiplier;
        // Keep audio pitch unchanged while slowing the simulation.
        timer->SetGlobalTimeMultiplier(multiplier, true);
    }

    void CombatSlowTime::Open(bool playerInCombat, bool enabled, float multiplier)
    {
        if (_active || !enabled || !playerInCombat) return;

        _previousMultiplier = ReadTimeMultiplier();
        const float appliedMultiplier =
            std::min(_previousMultiplier, ClampMultiplier(multiplier));
        if (appliedMultiplier >= _previousMultiplier - kOwnershipEpsilon) return;

        Apply(appliedMultiplier);
        _active = true;
        logger::info(
            "DragonBoardVR: combat slow time applied ({:.3f}; previous {:.3f}).",
            _appliedMultiplier,
            _previousMultiplier);
    }

    void CombatSlowTime::Close()
    {
        if (!_active) return;

        if (StillOwnsTimeMultiplier()) {
            if (auto* timer = RE::BSTimer::GetSingleton()) {
                timer->SetGlobalTimeMultiplier(_previousMultiplier, true);
                logger::info(
                    "DragonBoardVR: combat slow time restored to {:.3f}.",
                    _previousMultiplier);
            }
        } else {
            logger::info(
                "DragonBoardVR: combat slow time changed externally; leaving the current value unchanged.");
        }

        _active = false;
        _previousMultiplier = 1.0f;
        _appliedMultiplier = 1.0f;
    }

    void CombatSlowTime::Reconfigure(bool enabled, float multiplier)
    {
        if (!_active) return;
        if (!enabled) {
            Close();
            return;
        }
        if (!StillOwnsTimeMultiplier()) {
            logger::info(
                "DragonBoardVR: combat slow time changed externally during INI reload; relinquishing control.");
            _active = false;
            _previousMultiplier = 1.0f;
            _appliedMultiplier = 1.0f;
            return;
        }

        const float appliedMultiplier =
            std::min(_previousMultiplier, ClampMultiplier(multiplier));
        if (std::abs(appliedMultiplier - _appliedMultiplier) <= kOwnershipEpsilon) return;
        Apply(appliedMultiplier);
        logger::info(
            "DragonBoardVR: combat slow time updated from INI to {:.3f}.",
            _appliedMultiplier);
    }
}
