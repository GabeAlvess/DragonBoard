#pragma once

#include <RE/N/NiPoint3.h>

#include <memory>
#include <string>
#include <vector>

namespace RE
{
    class NiNode;
}

namespace vrui
{
    class VRUIContainer;
    class VRUIPanel;
}

namespace dragonboard::ui::panels
{
    struct PanelSwitchResult
    {
        bool found = false;
        std::shared_ptr<vrui::VRUIContainer> pageableContainer;
    };

    class PanelNavigator
    {
    public:
        static PanelSwitchResult SwitchTo(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const std::string& panelName,
            bool menuOpen,
            bool worldPinned,
            RE::NiNode* handNode,
            RE::NiNode* pinnedAttachNode,
            const RE::NiPoint3& panelOffset);
    };
}
