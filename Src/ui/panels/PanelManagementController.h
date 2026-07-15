#pragma once

#include <memory>
#include <string>

namespace RE
{
    class NiNode;
    class NiPoint3;
}

namespace vrui
{
    class VRMenuManager;
    class VRUIPanel;
}

namespace dragonboard::ui::panels
{
    class PanelManagementController
    {
    public:
        static void Register(vrui::VRMenuManager& manager, std::shared_ptr<vrui::VRUIPanel> panel);
        static void Unregister(
            vrui::VRMenuManager& manager,
            const std::shared_ptr<vrui::VRUIPanel>& panel);
        static RE::NiPoint3 GetPanelOffset();
        static RE::NiNode* ResolvePinnedAttachNode(
            const vrui::VRMenuManager& manager,
            RE::NiNode* skeletonRoot);
        static void SwitchTo(vrui::VRMenuManager& manager, const std::string& panelName);
        static std::shared_ptr<vrui::VRUIPanel> FindByName(
            vrui::VRMenuManager& manager,
            const std::string& name);
        static void SetWorldPinned(vrui::VRMenuManager& manager, bool pinned);
    };
}
