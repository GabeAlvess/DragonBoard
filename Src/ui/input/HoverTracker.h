#pragma once

#include <memory>

namespace vrui
{
    class VRUIWidget;
}

namespace dragonboard::ui::input
{
    struct HoverTransition
    {
        std::shared_ptr<vrui::VRUIWidget> previous;
        std::shared_ptr<vrui::VRUIWidget> current;
        bool changed = false;
    };

    class HoverTracker
    {
    public:
        HoverTransition Update(
            std::shared_ptr<vrui::VRUIWidget> candidate,
            float deltaTime,
            float lockDuration);
        std::shared_ptr<vrui::VRUIWidget> Get() const { return _hovered.lock(); }
        void Clear();

    private:
        std::weak_ptr<vrui::VRUIWidget> _hovered;
        float _lockTimer = 0.0f;
    };
}
