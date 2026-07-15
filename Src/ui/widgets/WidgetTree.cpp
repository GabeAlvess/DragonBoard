#include "WidgetTree.h"

#include "vrui/VRUIButton.h"
#include "vrui/VRUIContainer.h"
#include "vrui/VRUIDynamicContainer.h"

namespace dragonboard::ui::widgets
{
    void WidgetTree::CollectDynamicContainers(
        vrui::VRUIWidget* widget,
        std::vector<vrui::VRUIDynamicContainer*>& outContainers)
    {
        if (!widget) return;

        if (auto* dynamicContainer = dynamic_cast<vrui::VRUIDynamicContainer*>(widget)) {
            outContainers.push_back(dynamicContainer);
        }

        if (auto* container = dynamic_cast<vrui::VRUIContainer*>(widget)) {
            for (auto& child : container->getChildren()) {
                CollectDynamicContainers(child.get(), outContainers);
            }
        }
    }

    void WidgetTree::RefreshLabels(vrui::VRUIWidget* widget)
    {
        if (!widget) return;

        if (auto* button = dynamic_cast<vrui::VRUIButton*>(widget)) {
            button->refreshLabel();
        } else if (auto* container = dynamic_cast<vrui::VRUIContainer*>(widget)) {
            for (auto& child : container->getChildren()) {
                RefreshLabels(child.get());
            }
        }
    }

    void WidgetTree::UpdateContainerSpacing(
        vrui::VRUIWidget* widget,
        float spacingX,
        float spacingY,
        float spacingZ)
    {
        if (!widget) return;

        if (auto* container = dynamic_cast<vrui::VRUIContainer*>(widget)) {
            container->setSpacing(spacingX, spacingY, spacingZ);
            for (auto& child : container->getChildren()) {
                UpdateContainerSpacing(child.get(), spacingX, spacingY, spacingZ);
            }
        }
    }

    std::shared_ptr<vrui::VRUIContainer> WidgetTree::FindFirstGrid(vrui::VRUIWidget* widget)
    {
        if (!widget) return nullptr;

        for (auto& child : widget->getChildren()) {
            if (auto* container = dynamic_cast<vrui::VRUIContainer*>(child.get());
                container && container->getLayout() == vrui::ContainerLayout::Grid) {
                return std::dynamic_pointer_cast<vrui::VRUIContainer>(child);
            }
            if (auto found = FindFirstGrid(child.get())) {
                return found;
            }
        }

        return nullptr;
    }
}
