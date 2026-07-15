#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::menu
{
    class MenuInitializationController
    {
    public:
        static void Initialize(vrui::VRMenuManager& manager);
    };
}
