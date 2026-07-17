#pragma once

#include <string>

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::widgets
{
    class FixedWidgetPresenter
    {
    public:
        static void Refresh(vrui::VRMenuManager& menuManager);
        static void RefreshElement(vrui::VRMenuManager& menuManager, const std::string& elementId);
    };
}
