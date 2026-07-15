#include "PageNavigationState.h"

#include "vrui/VRUIContainer.h"

namespace dragonboard::ui::navigation
{
    void PageNavigationState::SetActive(
        const std::shared_ptr<vrui::VRUIContainer>& container)
    {
        _activeContainer = container;
    }

    void PageNavigationState::Next()
    {
        if (auto container = _activeContainer.lock()) {
            container->nextPage();
        }
    }

    void PageNavigationState::Previous()
    {
        if (auto container = _activeContainer.lock()) {
            container->prevPage();
        }
    }

    void PageNavigationState::Home()
    {
        if (auto container = _activeContainer.lock()) {
            container->setPage(0);
        }
    }
}
