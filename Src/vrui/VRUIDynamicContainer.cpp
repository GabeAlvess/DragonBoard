#include "VRUIDynamicContainer.h"

namespace vrui
{
    VRUIDynamicContainer::VRUIDynamicContainer(const std::string& name, 
                                             ContainerLayout layout,
                                             float spacing, 
                                             float scale)
        : VRUIContainer(name, layout, spacing, scale)
    {
    }

    void VRUIDynamicContainer::scheduleRefresh(float delaySeconds)
    {
        _pendingRefreshTimer = delaySeconds;
    }

    void VRUIDynamicContainer::update(float deltaTime)
    {
        VRUIWidget::update(deltaTime);

        if (_pendingRefreshTimer >= 0.0f) {
            _pendingRefreshTimer -= deltaTime;
            if (_pendingRefreshTimer <= 0.0f) {
                _pendingRefreshTimer = -1.0f;
                refresh();
                recalculateLayout();
                if (auto* node = getNode()) {
                    RE::NiUpdateData updateData;
                    updateData.flags = RE::NiUpdateData::Flag::kDirty;
                    node->Update(updateData);
                    node->UpdateWorldBound();
                }
            }
        }
    }

}
