#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::input
{
    class PointerInteractionController
    {
    public:
        static void Process(vrui::VRMenuManager& manager, float deltaTime);
    };
}
