#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::menu
{
    class MenuLifecycleController
    {
    public:
        static void ApplyToggle(vrui::VRMenuManager& manager, bool suppressToggleHaptic = false);
        static void ApplySafeClose(vrui::VRMenuManager& manager);
    };
}
