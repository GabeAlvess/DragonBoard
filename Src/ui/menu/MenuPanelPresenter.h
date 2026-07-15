#pragma once

#include <RE/N/NiPoint3.h>

#include <memory>
#include <vector>

namespace RE
{
    class NiNode;
}

namespace vrui
{
    class VRUIPanel;
}

namespace dragonboard::ui::menu
{
    class MenuPanelPresenter
    {
    public:
        static void PresentOpen(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            bool worldPinned,
            RE::NiNode* menuHand,
            RE::NiNode* pinnedAttachNode,
            const RE::NiPoint3& panelOffset);
        static void PresentClosed(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            bool worldPinned,
            RE::NiNode* menuHand,
            RE::NiNode* pinnedAttachNode,
            RE::NiNode* headNode,
            const RE::NiPoint3& panelOffset);
        static void DetachAll(const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels);
    };
}
