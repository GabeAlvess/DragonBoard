#pragma once

#include <memory>
#include <vector>

namespace vrui
{
    class VRUIPanel;
}

namespace dragonboard::ui::refresh
{
    class PanelRefresher
    {
    public:
        static void UpdateEquippedStates(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels);
        static void RefreshDynamic(
            const std::shared_ptr<vrui::VRUIPanel>& panel,
            float spacingX,
            float spacingY);
        static void RefreshLayouts(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            float spacingX,
            float spacingY);
    };
}
