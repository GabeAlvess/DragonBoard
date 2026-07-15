#pragma once

#include "HoverTracker.h"

#include <memory>
#include <utility>

namespace dragonboard::ui::input
{
    class InteractionFocusState
    {
    public:
        HoverTransition UpdateHover(
            std::shared_ptr<vrui::VRUIWidget> candidate,
            float deltaTime,
            float lockDuration)
        {
            return _hover.Update(std::move(candidate), deltaTime, lockDuration);
        }

        std::shared_ptr<vrui::VRUIWidget> GetHovered() const { return _hover.Get(); }
        void ClearHover() { _hover.Clear(); }

        std::shared_ptr<vrui::VRUIWidget> GetGrabbed() const { return _grabbed.lock(); }
        bool HasGrabbed() const { return !_grabbed.expired(); }
        void SetGrabbed(const std::shared_ptr<vrui::VRUIWidget>& widget) { _grabbed = widget; }
        void ClearGrabbed(vrui::VRUIWidget* expected = nullptr)
        {
            auto grabbed = _grabbed.lock();
            if (!expected || (grabbed && grabbed.get() == expected)) {
                _grabbed.reset();
            }
        }

    private:
        HoverTracker _hover;
        std::weak_ptr<vrui::VRUIWidget> _grabbed;
    };
}
