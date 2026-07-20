#pragma once

#include <functional>

namespace vrui
{
    class VRMenuManager;
}

namespace RE
{
    class TESForm;
}

namespace dragonboard::ui::equipment
{
    class EquipInteractionController
    {
    public:
        static bool CanExecute(const vrui::VRMenuManager& manager);
        static void NotifyExecuted(vrui::VRMenuManager& manager);
        static void RequestRefresh(vrui::VRMenuManager& manager, float delay);
        static void PerformSkeletonSafeChange(
            vrui::VRMenuManager& manager,
            std::function<void()> change);
        static bool RequiresSkeletonBridge(const RE::TESForm* form);
        static void NotifyPlayerEquipmentChanged();
        static void Update(vrui::VRMenuManager& manager, float deltaTime);
    };
}
