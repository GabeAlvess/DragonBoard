#pragma once

#include <memory>

namespace vrui
{
    class VRUIContainer;
}

namespace dragonboard::ui::navigation
{
    class PageNavigationState
    {
    public:
        void SetActive(const std::shared_ptr<vrui::VRUIContainer>& container);
        void Home();

    private:
        std::weak_ptr<vrui::VRUIContainer> _activeContainer;
    };
}
