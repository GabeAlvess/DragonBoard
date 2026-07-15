#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::equipment
{
    class EquipInteractionController
    {
    public:
        static bool CanExecute(const vrui::VRMenuManager& manager);
        static void NotifyExecuted(vrui::VRMenuManager& manager);
        static void RequestRefresh(vrui::VRMenuManager& manager, float delay);
        static void Update(vrui::VRMenuManager& manager, float deltaTime);
    };
}
