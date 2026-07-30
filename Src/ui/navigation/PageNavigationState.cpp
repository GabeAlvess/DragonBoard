#include "PageNavigationState.h"

#include "vrui/VRUIContainer.h"

namespace dragonboard::ui::navigation
{
    void PageNavigationState::SetActive(
        const std::shared_ptr<vrui::VRUIContainer>& container)
    {
        _activeContainer = container;
    }

    void PageNavigationState::Home()
    {
        if (auto container = _activeContainer.lock()) {
            container->setPage(0);
        }
    }
}
