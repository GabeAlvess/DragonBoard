#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::frame
{
    class FrameUpdateController
    {
    public:
        static void Update(vrui::VRMenuManager& manager, float deltaTime);
    };
}
