#include "HoverTracker.h"

#include <utility>

namespace dragonboard::ui::input
{
    HoverTransition HoverTracker::Update(
        std::shared_ptr<vrui::VRUIWidget> candidate,
        float deltaTime,
        float lockDuration)
    {
        if (_lockTimer > 0.0f) {
            _lockTimer -= deltaTime;
        }

        auto previous = _hovered.lock();
        if (previous) {
            if (candidate == previous) {
                _lockTimer = lockDuration;
            } else if (_lockTimer > 0.0f) {
                candidate = previous;
            }
        }

        const bool changed = candidate != previous;
        if (changed) {
            _hovered = candidate;
            if (candidate) {
                _lockTimer = lockDuration;
            }
        }

        return { std::move(previous), std::move(candidate), changed };
    }

    void HoverTracker::Clear()
    {
        _hovered.reset();
        _lockTimer = 0.0f;
    }
}
