#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::input
{
    class InteractionInputController
    {
    public:
        static void ProcessActivation(vrui::VRMenuManager& manager, float deltaTime);
        static void ProcessButtons(vrui::VRMenuManager& manager, float deltaTime);
    };
}
