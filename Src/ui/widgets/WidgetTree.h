#pragma once

#include <memory>
#include <vector>

namespace vrui
{
    class VRUIWidget;
    class VRUIContainer;
    class VRUIDynamicContainer;
}

namespace dragonboard::ui::widgets
{
    class WidgetTree
    {
    public:
        static void CollectDynamicContainers(
            vrui::VRUIWidget* widget,
            std::vector<vrui::VRUIDynamicContainer*>& outContainers);
        static void UpdateContainerSpacing(
            vrui::VRUIWidget* widget,
            float spacingX,
            float spacingY,
            float spacingZ);
        static std::shared_ptr<vrui::VRUIContainer> FindFirstGrid(vrui::VRUIWidget* widget);
    };
}
