#pragma once

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
    };
}
